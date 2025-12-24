#if defined(__linux__) && !defined(BACKEND_USE_X11)

#include <typr-io/sender.hpp>

#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <thread>
#include <typr-io/log.hpp>
#include <unistd.h>
#include <unordered_map>
#include <xkbcommon/xkbcommon.h>

namespace typr::io {

namespace {

// Per-instance key maps are preferred (layout-aware discovery or future
// runtime overrides). The uinput backend initializes a per-Impl map in
// its constructor via Impl::initKeyMap() to mirror the macOS style.
//
// We use xkbcommon to perform a layout-aware scan of physical keycodes
// (mirroring the Windows/macOS approach of probing physical scan codes
// and mapping them via the active layout). The helper below mirrors the
// listener's keysym -> Key mapping logic so that discovered mappings are
// consistent with what the listener reports.
static Key mapKeysymToKey(xkb_keysym_t sym) {
  // Quick alphabetic mapping (lowercase and uppercase)
  if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
    return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_a));
  }
  if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) {
    return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_A));
  }

  // Top-row numbers
  if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) {
    return static_cast<Key>(static_cast<int>(Key::Num0) + (sym - XKB_KEY_0));
  }

  // Function keys
  if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F20) {
    return static_cast<Key>(static_cast<int>(Key::F1) + (sym - XKB_KEY_F1));
  }

  // Direct mappings for common control keys
  switch (sym) {
  case XKB_KEY_Return:
    return Key::Enter;
  case XKB_KEY_BackSpace:
    return Key::Backspace;
  case XKB_KEY_space:
    return Key::Space;
  case XKB_KEY_Tab:
    return Key::Tab;
  case XKB_KEY_Escape:
    return Key::Escape;
  case XKB_KEY_Left:
    return Key::Left;
  case XKB_KEY_Right:
    return Key::Right;
  case XKB_KEY_Up:
    return Key::Up;
  case XKB_KEY_Down:
    return Key::Down;
  case XKB_KEY_Home:
    return Key::Home;
  case XKB_KEY_End:
    return Key::End;
  case XKB_KEY_Page_Up:
    return Key::PageUp;
  case XKB_KEY_Page_Down:
    return Key::PageDown;
  case XKB_KEY_Delete:
    return Key::Delete;
  case XKB_KEY_Insert:
    return Key::Insert;
  // Numpad
  case XKB_KEY_KP_Divide:
    return Key::NumpadDivide;
  case XKB_KEY_KP_Multiply:
    return Key::NumpadMultiply;
  case XKB_KEY_KP_Subtract:
    return Key::NumpadMinus;
  case XKB_KEY_KP_Add:
    return Key::NumpadPlus;
  case XKB_KEY_KP_Enter:
    return Key::NumpadEnter;
  case XKB_KEY_KP_Decimal:
    return Key::NumpadDecimal;
  case XKB_KEY_KP_0:
    return Key::Numpad0;
  case XKB_KEY_KP_1:
    return Key::Numpad1;
  case XKB_KEY_KP_2:
    return Key::Numpad2;
  case XKB_KEY_KP_3:
    return Key::Numpad3;
  case XKB_KEY_KP_4:
    return Key::Numpad4;
  case XKB_KEY_KP_5:
    return Key::Numpad5;
  case XKB_KEY_KP_6:
    return Key::Numpad6;
  case XKB_KEY_KP_7:
    return Key::Numpad7;
  case XKB_KEY_KP_8:
    return Key::Numpad8;
  case XKB_KEY_KP_9:
    return Key::Numpad9;
  // Common punctuation
  case XKB_KEY_comma:
    return Key::Comma;
  case XKB_KEY_period:
    return Key::Period;
  case XKB_KEY_slash:
    return Key::Slash;
  case XKB_KEY_backslash:
    return Key::Backslash;
  case XKB_KEY_semicolon:
    return Key::Semicolon;
  case XKB_KEY_apostrophe:
    return Key::Apostrophe;
  case XKB_KEY_minus:
    return Key::Minus;
  case XKB_KEY_equal:
    return Key::Equal;
  case XKB_KEY_grave:
    return Key::Grave;
  case XKB_KEY_bracketleft:
    return Key::LeftBracket;
  case XKB_KEY_bracketright:
    return Key::RightBracket;
  default:
    break;
  }

  // Try to map using the keysym name -> stringToKey as a best-effort fallback
  char name[64] = {0};
  if (xkb_keysym_get_name(sym, name, sizeof(name)) > 0) {
    Key mapped = stringToKey(std::string(name));
    if (mapped != Key::Unknown)
      return mapped;
  }

  return Key::Unknown;
}
} // namespace

struct Sender::Impl {
  int fd{-1};
  Modifier currentMods{Modifier::None};
  uint32_t keyDelayUs{1000};
  std::unordered_map<Key, int> keyMap;

  Impl() {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
      TYPR_IO_LOG_ERROR("Sender (uinput): failed to open /dev/uinput: %s",
                        strerror(errno));
      return;
    }

    // Enable key events
    ioctl(fd, UI_SET_EVBIT, EV_KEY);

#ifdef KEY_MAX
    // Enable all key codes we might use (if available)
    for (int i = 0; i < KEY_MAX; ++i) {
      ioctl(fd, UI_SET_KEYBIT, i);
    }
#endif

    // Create virtual device
    struct uinput_setup usetup{};
    std::memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    std::strncpy(usetup.name, "Virtual Keyboard", UINPUT_MAX_NAME_SIZE - 1);

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    // Give udev time to create the device node
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Initialize the per-instance key map (layout-aware logic can be added
    // later)
    initKeyMap();
    TYPR_IO_LOG_INFO(
        "Sender (uinput): device initialized fd=%d keymap_entries=%zu", fd,
        keyMap.size());
  }

  ~Impl() {
    if (fd >= 0) {
      ioctl(fd, UI_DEV_DESTROY);
      close(fd);
      TYPR_IO_LOG_INFO("Sender (uinput): device destroyed (fd=%d)", fd);
    }
  }

  // Non-copyable, movable
  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;

  Impl(Impl &&other) noexcept
      : fd(other.fd), currentMods(other.currentMods),
        keyDelayUs(other.keyDelayUs), keyMap(std::move(other.keyMap)) {
    other.fd = -1;
    other.currentMods = Modifier::None;
    other.keyDelayUs = 0;
  }

  Impl &operator=(Impl &&other) noexcept {
    if (this == &other)
      return *this;
    if (fd >= 0) {
      ioctl(fd, UI_DEV_DESTROY);
      close(fd);
    }
    fd = other.fd;
    currentMods = other.currentMods;
    keyDelayUs = other.keyDelayUs;
    keyMap = std::move(other.keyMap);

    other.fd = -1;
    other.currentMods = Modifier::None;
    other.keyDelayUs = 0;
    return *this;
  }

  void initKeyMap() {
    // Clear any existing mappings and attempt a layout-aware discovery of
    // physical keys using xkbcommon. This mirrors how the listener resolves
    // keys (via XKB) and mirrors the Windows/macOS approach of scanning
    // physical keycodes and mapping them via the active layout.
    keyMap.clear();

    struct xkb_context *xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (xkbCtx) {
      struct xkb_keymap *xkbKeymap = xkb_keymap_new_from_names(
          xkbCtx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
      if (xkbKeymap) {
        struct xkb_state *xkbState = xkb_state_new(xkbKeymap);
        if (xkbState) {
          xkb_keycode_t min = xkb_keymap_min_keycode(xkbKeymap);
          xkb_keycode_t max = xkb_keymap_max_keycode(xkbKeymap);
          for (xkb_keycode_t xkbKey = min; xkbKey <= max; ++xkbKey) {
            xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState, xkbKey);
            if (sym == XKB_KEY_NoSymbol)
              continue;
            Key mapped = mapKeysymToKey(sym);
            if (mapped == Key::Unknown)
              continue;

            // libinput provides evdev keycodes; xkbcommon uses an offset of
            // 8. When sending via uinput we need the evdev keycode (the same
            // numeric values as KEY_* constants).
            int evdevCode = static_cast<int>(xkbKey - 8);
            if (evdevCode > 0) {
              if (keyMap.find(mapped) == keyMap.end()) {
                keyMap[mapped] = evdevCode;
              }
            }
          }
          xkb_state_unref(xkbState);
        } else {
          TYPR_IO_LOG_ERROR("Sender (uinput): xkb_state_new() failed");
        }
        xkb_keymap_unref(xkbKeymap);
      } else {
        TYPR_IO_LOG_ERROR(
            "Sender (uinput): xkb_keymap_new_from_names() failed");
      }
      xkb_context_unref(xkbCtx);
    } else {
      TYPR_IO_LOG_ERROR("Sender (uinput): xkb_context_new() failed");
    }

    // Fallback explicit mappings for common non-printable keys / modifiers
    auto setIfMissing = [this](Key k, int v) {
      if (this->keyMap.find(k) == this->keyMap.end())
        this->keyMap[k] = v;
    };

    // Common keys
    setIfMissing(Key::Space, KEY_SPACE);
    setIfMissing(Key::Enter, KEY_ENTER);
    setIfMissing(Key::Tab, KEY_TAB);
    setIfMissing(Key::Backspace, KEY_BACKSPACE);
    setIfMissing(Key::Delete, KEY_DELETE);
    setIfMissing(Key::Escape, KEY_ESC);
    setIfMissing(Key::Left, KEY_LEFT);
    setIfMissing(Key::Right, KEY_RIGHT);
    setIfMissing(Key::Up, KEY_UP);
    setIfMissing(Key::Down, KEY_DOWN);
    setIfMissing(Key::Home, KEY_HOME);
    setIfMissing(Key::End, KEY_END);
    setIfMissing(Key::PageUp, KEY_PAGEUP);
    setIfMissing(Key::PageDown, KEY_PAGEDOWN);

    // Modifiers
    setIfMissing(Key::ShiftLeft, KEY_LEFTSHIFT);
    setIfMissing(Key::ShiftRight, KEY_RIGHTSHIFT);
    setIfMissing(Key::CtrlLeft, KEY_LEFTCTRL);
    setIfMissing(Key::CtrlRight, KEY_RIGHTCTRL);
    setIfMissing(Key::AltLeft, KEY_LEFTALT);
    setIfMissing(Key::AltRight, KEY_RIGHTALT);
    setIfMissing(Key::SuperLeft, KEY_LEFTMETA);
    setIfMissing(Key::SuperRight, KEY_RIGHTMETA);
    setIfMissing(Key::CapsLock, KEY_CAPSLOCK);
    setIfMissing(Key::NumLock, KEY_NUMLOCK);

    // Function keys
    setIfMissing(Key::F1, KEY_F1);
    setIfMissing(Key::F2, KEY_F2);
    setIfMissing(Key::F3, KEY_F3);
    setIfMissing(Key::F4, KEY_F4);
    setIfMissing(Key::F5, KEY_F5);
    setIfMissing(Key::F6, KEY_F6);
    setIfMissing(Key::F7, KEY_F7);
    setIfMissing(Key::F8, KEY_F8);
    setIfMissing(Key::F9, KEY_F9);
    setIfMissing(Key::F10, KEY_F10);
    setIfMissing(Key::F11, KEY_F11);
    setIfMissing(Key::F12, KEY_F12);
    setIfMissing(Key::F13, KEY_F13);
    setIfMissing(Key::F14, KEY_F14);
    setIfMissing(Key::F15, KEY_F15);
    setIfMissing(Key::F16, KEY_F16);
    setIfMissing(Key::F17, KEY_F17);
    setIfMissing(Key::F18, KEY_F18);
    setIfMissing(Key::F19, KEY_F19);
    setIfMissing(Key::F20, KEY_F20);

    // Letters fallback
    setIfMissing(Key::A, KEY_A);
    setIfMissing(Key::B, KEY_B);
    setIfMissing(Key::C, KEY_C);
    setIfMissing(Key::D, KEY_D);
    setIfMissing(Key::E, KEY_E);
    setIfMissing(Key::F, KEY_F);
    setIfMissing(Key::G, KEY_G);
    setIfMissing(Key::H, KEY_H);
    setIfMissing(Key::I, KEY_I);
    setIfMissing(Key::J, KEY_J);
    setIfMissing(Key::K, KEY_K);
    setIfMissing(Key::L, KEY_L);
    setIfMissing(Key::M, KEY_M);
    setIfMissing(Key::N, KEY_N);
    setIfMissing(Key::O, KEY_O);
    setIfMissing(Key::P, KEY_P);
    setIfMissing(Key::Q, KEY_Q);
    setIfMissing(Key::R, KEY_R);
    setIfMissing(Key::S, KEY_S);
    setIfMissing(Key::T, KEY_T);
    setIfMissing(Key::U, KEY_U);
    setIfMissing(Key::V, KEY_V);
    setIfMissing(Key::W, KEY_W);
    setIfMissing(Key::X, KEY_X);
    setIfMissing(Key::Y, KEY_Y);
    setIfMissing(Key::Z, KEY_Z);

    // Top-row numbers (unshifted)
    setIfMissing(Key::Num0, KEY_0);
    setIfMissing(Key::Num1, KEY_1);
    setIfMissing(Key::Num2, KEY_2);
    setIfMissing(Key::Num3, KEY_3);
    setIfMissing(Key::Num4, KEY_4);
    setIfMissing(Key::Num5, KEY_5);
    setIfMissing(Key::Num6, KEY_6);
    setIfMissing(Key::Num7, KEY_7);
    setIfMissing(Key::Num8, KEY_8);
    setIfMissing(Key::Num9, KEY_9);

    // Numpad
    setIfMissing(Key::Numpad0, KEY_KP0);
    setIfMissing(Key::Numpad1, KEY_KP1);
    setIfMissing(Key::Numpad2, KEY_KP2);
    setIfMissing(Key::Numpad3, KEY_KP3);
    setIfMissing(Key::Numpad4, KEY_KP4);
    setIfMissing(Key::Numpad5, KEY_KP5);
    setIfMissing(Key::Numpad6, KEY_KP6);
    setIfMissing(Key::Numpad7, KEY_KP7);
    setIfMissing(Key::Numpad8, KEY_KP8);
    setIfMissing(Key::Numpad9, KEY_KP9);
    setIfMissing(Key::NumpadDivide, KEY_KPSLASH);
    setIfMissing(Key::NumpadMultiply, KEY_KPASTERISK);
    setIfMissing(Key::NumpadMinus, KEY_KPMINUS);
    setIfMissing(Key::NumpadPlus, KEY_KPPLUS);
    setIfMissing(Key::NumpadEnter, KEY_KPENTER);
    setIfMissing(Key::NumpadDecimal, KEY_KPDOT);

    // Misc
    setIfMissing(Key::Menu, KEY_MENU);
    setIfMissing(Key::Mute, KEY_MUTE);
    setIfMissing(Key::VolumeDown, KEY_VOLUMEDOWN);
    setIfMissing(Key::VolumeUp, KEY_VOLUMEUP);
    setIfMissing(Key::MediaPlayPause, KEY_PLAYPAUSE);
    setIfMissing(Key::MediaStop, KEY_STOPCD);
    setIfMissing(Key::MediaNext, KEY_NEXTSONG);
    setIfMissing(Key::MediaPrevious, KEY_PREVIOUSSONG);

    // Punctuation / layout-dependent
    setIfMissing(Key::Grave, KEY_GRAVE);
    setIfMissing(Key::Minus, KEY_MINUS);
    setIfMissing(Key::Equal, KEY_EQUAL);
    setIfMissing(Key::LeftBracket, KEY_LEFTBRACE);
    setIfMissing(Key::RightBracket, KEY_RIGHTBRACE);
    setIfMissing(Key::Backslash, KEY_BACKSLASH);
    setIfMissing(Key::Semicolon, KEY_SEMICOLON);
    setIfMissing(Key::Apostrophe, KEY_APOSTROPHE);
    setIfMissing(Key::Comma, KEY_COMMA);
    setIfMissing(Key::Period, KEY_DOT);
    setIfMissing(Key::Slash, KEY_SLASH);

    TYPR_IO_LOG_DEBUG("Sender (uinput): initKeyMap populated %zu entries",
                      keyMap.size());
  }

  int linuxKeyCodeFor(Key key) const {
    auto it = keyMap.find(key);
    if (it == keyMap.end()) {
      TYPR_IO_LOG_DEBUG("Sender (uinput): no mapping for key=%s",
                        keyToString(key).c_str());
      return -1;
    }
    return it->second;
  }

  void emit(int type, int code, int val) {
    struct input_event ev{};
    ev.type = static_cast<unsigned short>(type);
    ev.code = static_cast<unsigned short>(code);
    ev.value = val;
    ssize_t wrote = write(fd, &ev, sizeof(ev));
    if (wrote < 0) {
      TYPR_IO_LOG_ERROR(
          "Sender (uinput): write failed (type=%d code=%d val=%d): %s", type,
          code, val, strerror(errno));
    } else {
      TYPR_IO_LOG_DEBUG(
          "Sender (uinput): emit type=%d code=%d val=%d wrote=%zd", type, code,
          val, wrote);
    }
  }

  void sync() {
    emit(EV_SYN, SYN_REPORT, 0);
    TYPR_IO_LOG_DEBUG("Sender (uinput): sync()");
  }

  bool sendKey(Key key, bool down) {
    if (fd < 0) {
      TYPR_IO_LOG_ERROR("Sender (uinput): device not ready (fd < 0)");
      return false;
    }

    int code = linuxKeyCodeFor(key);
    if (code < 0) {
      TYPR_IO_LOG_DEBUG("Sender (uinput): sendKey - no code mapping for %s",
                        keyToString(key).c_str());
      return false;
    }

    emit(EV_KEY, code, down ? 1 : 0);
    sync();
    TYPR_IO_LOG_DEBUG("Sender (uinput): sendKey %s code=%d %s",
                      keyToString(key).c_str(), code, down ? "down" : "up");
    return true;
  }

  void delay() {
    if (keyDelayUs > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(keyDelayUs));
    }
  }
};

Sender::Sender() : m_impl(std::make_unique<Impl>()) {
  TYPR_IO_LOG_INFO("Sender (uinput): constructed, ready=%u",
                   static_cast<unsigned>(isReady()));
}
Sender::~Sender() = default;
Sender::Sender(Sender &&) noexcept = default;
Sender &Sender::operator=(Sender &&) noexcept = default;

BackendType Sender::type() const {
  TYPR_IO_LOG_DEBUG("Sender::type() -> LinuxUInput");
  return BackendType::LinuxUInput;
}

Capabilities Sender::capabilities() const {
  TYPR_IO_LOG_DEBUG("Sender::capabilities() called");
  return {
      .canInjectKeys = (m_impl && m_impl->fd >= 0),
      .canInjectText = false, // uinput is physical keys only
      .canSimulateHID = true, // This is true HID simulation
      .supportsKeyRepeat = true,
      .needsAccessibilityPerm = false,
      .needsInputMonitoringPerm = false,
      .needsUinputAccess = true,
  };
}

bool Sender::isReady() const {
  bool ready = (m_impl && m_impl->fd >= 0);
  TYPR_IO_LOG_DEBUG("Sender::isReady() -> %u", static_cast<unsigned>(ready));
  return ready;
}

bool Sender::requestPermissions() {
  TYPR_IO_LOG_DEBUG("Sender::requestPermissions() called");
  // Can't request at runtime - needs /dev/uinput access (udev rules or root)
  return isReady();
}

bool Sender::keyDown(Key key) {
  if (!m_impl)
    return false;
  TYPR_IO_LOG_DEBUG("Sender::keyDown(%s)", keyToString(key).c_str());

  switch (key) {
  case Key::ShiftLeft:
  case Key::ShiftRight:
    m_impl->currentMods = m_impl->currentMods | Modifier::Shift;
    break;
  case Key::CtrlLeft:
  case Key::CtrlRight:
    m_impl->currentMods = m_impl->currentMods | Modifier::Ctrl;
    break;
  case Key::AltLeft:
  case Key::AltRight:
    m_impl->currentMods = m_impl->currentMods | Modifier::Alt;
    break;
  case Key::SuperLeft:
  case Key::SuperRight:
    m_impl->currentMods = m_impl->currentMods | Modifier::Super;
    break;
  default:
    break;
  }
  bool ok = m_impl->sendKey(key, true);
  TYPR_IO_LOG_DEBUG("Sender::keyDown(%s) result=%u", keyToString(key).c_str(),
                    static_cast<unsigned>(ok));
  return ok;
}

bool Sender::keyUp(Key key) {
  if (!m_impl)
    return false;
  TYPR_IO_LOG_DEBUG("Sender::keyUp(%s)", keyToString(key).c_str());

  bool result = m_impl->sendKey(key, false);
  switch (key) {
  case Key::ShiftLeft:
  case Key::ShiftRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<uint8_t>(m_impl->currentMods) &
                              ~static_cast<uint8_t>(Modifier::Shift));
    break;
  case Key::CtrlLeft:
  case Key::CtrlRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<uint8_t>(m_impl->currentMods) &
                              ~static_cast<uint8_t>(Modifier::Ctrl));
    break;
  case Key::AltLeft:
  case Key::AltRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<uint8_t>(m_impl->currentMods) &
                              ~static_cast<uint8_t>(Modifier::Alt));
    break;
  case Key::SuperLeft:
  case Key::SuperRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<uint8_t>(m_impl->currentMods) &
                              ~static_cast<uint8_t>(Modifier::Super));
    break;
  default:
    break;
  }
  TYPR_IO_LOG_DEBUG("Sender::keyUp(%s) result=%u", keyToString(key).c_str(),
                    static_cast<unsigned>(result));
  return result;
}

bool Sender::tap(Key key) {
  TYPR_IO_LOG_DEBUG("Sender::tap(%s)", keyToString(key).c_str());
  if (!keyDown(key))
    return false;
  m_impl->delay();
  bool ok = keyUp(key);
  TYPR_IO_LOG_DEBUG("Sender::tap(%s) result=%u", keyToString(key).c_str(),
                    static_cast<unsigned>(ok));
  return ok;
}

Modifier Sender::activeModifiers() const {
  Modifier mods = m_impl ? m_impl->currentMods : Modifier::None;
  TYPR_IO_LOG_DEBUG("Sender::activeModifiers() -> %u",
                    static_cast<unsigned>(mods));
  return mods;
}

bool Sender::holdModifier(Modifier mod) {
  TYPR_IO_LOG_DEBUG("Sender::holdModifier(mod=%u)", static_cast<unsigned>(mod));
  bool ok = true;
  if (hasModifier(mod, Modifier::Shift))
    ok &= keyDown(Key::ShiftLeft);
  if (hasModifier(mod, Modifier::Ctrl))
    ok &= keyDown(Key::CtrlLeft);
  if (hasModifier(mod, Modifier::Alt))
    ok &= keyDown(Key::AltLeft);
  if (hasModifier(mod, Modifier::Super))
    ok &= keyDown(Key::SuperLeft);
  TYPR_IO_LOG_DEBUG(
      "Sender::holdModifier result=%u currentMods=%u",
      static_cast<unsigned>(ok),
      static_cast<unsigned>(m_impl ? m_impl->currentMods : Modifier::None));
  return ok;
}

bool Sender::releaseModifier(Modifier mod) {
  TYPR_IO_LOG_DEBUG("Sender::releaseModifier(mod=%u)",
                    static_cast<unsigned>(mod));
  bool ok = true;
  if (hasModifier(mod, Modifier::Shift))
    ok &= keyUp(Key::ShiftLeft);
  if (hasModifier(mod, Modifier::Ctrl))
    ok &= keyUp(Key::CtrlLeft);
  if (hasModifier(mod, Modifier::Alt))
    ok &= keyUp(Key::AltLeft);
  if (hasModifier(mod, Modifier::Super))
    ok &= keyUp(Key::SuperLeft);
  TYPR_IO_LOG_DEBUG(
      "Sender::releaseModifier result=%u currentMods=%u",
      static_cast<unsigned>(ok),
      static_cast<unsigned>(m_impl ? m_impl->currentMods : Modifier::None));
  return ok;
}

bool Sender::releaseAllModifiers() {
  TYPR_IO_LOG_DEBUG("Sender::releaseAllModifiers()");
  bool ok = releaseModifier(Modifier::Shift | Modifier::Ctrl | Modifier::Alt |
                            Modifier::Super);
  TYPR_IO_LOG_DEBUG("Sender::releaseAllModifiers result=%u",
                    static_cast<unsigned>(ok));
  return ok;
}

bool Sender::combo(Modifier mods, Key key) {
  TYPR_IO_LOG_DEBUG("Sender::combo(mods=%u key=%s)",
                    static_cast<unsigned>(mods), keyToString(key).c_str());
  if (!holdModifier(mods))
    return false;
  m_impl->delay();
  bool ok = tap(key);
  m_impl->delay();
  releaseModifier(mods);
  TYPR_IO_LOG_DEBUG("Sender::combo result=%u", static_cast<unsigned>(ok));
  return ok;
}

bool Sender::typeText(const std::u32string & /*text*/) {
  TYPR_IO_LOG_INFO("Sender (uinput): typeText called but uinput cannot inject "
                   "Unicode directly");
  // uinput cannot inject Unicode directly; converting to key events depends
  // on keyboard layout and is outside the scope of this backend.
  return false;
}

bool Sender::typeText(const std::string & /*utf8Text*/) {
  TYPR_IO_LOG_INFO("Sender (uinput): typeText(utf8) called but not supported "
                   "by uinput backend");
  return false;
}

bool Sender::typeCharacter(char32_t /*codepoint*/) {
  TYPR_IO_LOG_INFO("Sender (uinput): typeCharacter called but not supported by "
                   "uinput backend");
  return false;
}

void Sender::flush() {
  TYPR_IO_LOG_DEBUG("Sender::flush()");
  if (m_impl)
    m_impl->sync();
}

void Sender::setKeyDelay(uint32_t delayUs) {
  TYPR_IO_LOG_DEBUG("Sender::setKeyDelay(%u)", delayUs);
  if (m_impl)
    m_impl->keyDelayUs = delayUs;
}

} // namespace typr::io

#endif // __linux__ && !BACKEND_USE_X11
