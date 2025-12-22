#if defined(__linux__) && !defined(BACKEND_USE_X11)

#include <typr-io/sender.hpp>

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace typr::io {

namespace {

// Per-instance key maps are preferred (layout-aware discovery or future
// runtime overrides). The uinput backend initializes a per-Impl map in
// its constructor via Impl::initKeyMap() to mirror the macOS style.
} // namespace

struct Sender::Impl {
  int fd{-1};
  Modifier currentMods{Modifier::None};
  uint32_t keyDelayUs{1000};
  std::unordered_map<Key, int> keyMap;

  Impl() {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0)
      return;

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
  }

  ~Impl() {
    if (fd >= 0) {
      ioctl(fd, UI_DEV_DESTROY);
      close(fd);
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
    // Populate a per-instance mapping from our Key enum to Linux keycodes.
    // These are the same mappings as before, now owned per-Impl so that they
    // can be adjusted at runtime if needed (layout detection, user overrides).
    keyMap = {
        // Letters
        {Key::A, KEY_A},
        {Key::B, KEY_B},
        {Key::C, KEY_C},
        {Key::D, KEY_D},
        {Key::E, KEY_E},
        {Key::F, KEY_F},
        {Key::G, KEY_G},
        {Key::H, KEY_H},
        {Key::I, KEY_I},
        {Key::J, KEY_J},
        {Key::K, KEY_K},
        {Key::L, KEY_L},
        {Key::M, KEY_M},
        {Key::N, KEY_N},
        {Key::O, KEY_O},
        {Key::P, KEY_P},
        {Key::Q, KEY_Q},
        {Key::R, KEY_R},
        {Key::S, KEY_S},
        {Key::T, KEY_T},
        {Key::U, KEY_U},
        {Key::V, KEY_V},
        {Key::W, KEY_W},
        {Key::X, KEY_X},
        {Key::Y, KEY_Y},
        {Key::Z, KEY_Z},

        // Numbers (top row)
        {Key::Num0, KEY_0},
        {Key::Num1, KEY_1},
        {Key::Num2, KEY_2},
        {Key::Num3, KEY_3},
        {Key::Num4, KEY_4},
        {Key::Num5, KEY_5},
        {Key::Num6, KEY_6},
        {Key::Num7, KEY_7},
        {Key::Num8, KEY_8},
        {Key::Num9, KEY_9},

        // Function keys
        {Key::F1, KEY_F1},
        {Key::F2, KEY_F2},
        {Key::F3, KEY_F3},
        {Key::F4, KEY_F4},
        {Key::F5, KEY_F5},
        {Key::F6, KEY_F6},
        {Key::F7, KEY_F7},
        {Key::F8, KEY_F8},
        {Key::F9, KEY_F9},
        {Key::F10, KEY_F10},
        {Key::F11, KEY_F11},
        {Key::F12, KEY_F12},
        {Key::F13, KEY_F13},
        {Key::F14, KEY_F14},
        {Key::F15, KEY_F15},
        {Key::F16, KEY_F16},
        {Key::F17, KEY_F17},
        {Key::F18, KEY_F18},
        {Key::F19, KEY_F19},
        {Key::F20, KEY_F20},

        // Control
        {Key::Enter, KEY_ENTER},
        {Key::Escape, KEY_ESC},
        {Key::Backspace, KEY_BACKSPACE},
        {Key::Tab, KEY_TAB},
        {Key::Space, KEY_SPACE},

        // Navigation
        {Key::Left, KEY_LEFT},
        {Key::Right, KEY_RIGHT},
        {Key::Up, KEY_UP},
        {Key::Down, KEY_DOWN},
        {Key::Home, KEY_HOME},
        {Key::End, KEY_END},
        {Key::PageUp, KEY_PAGEUP},
        {Key::PageDown, KEY_PAGEDOWN},
        {Key::Delete, KEY_DELETE},
        {Key::Insert, KEY_INSERT},

        // Numpad
        {Key::Numpad0, KEY_KP0},
        {Key::Numpad1, KEY_KP1},
        {Key::Numpad2, KEY_KP2},
        {Key::Numpad3, KEY_KP3},
        {Key::Numpad4, KEY_KP4},
        {Key::Numpad5, KEY_KP5},
        {Key::Numpad6, KEY_KP6},
        {Key::Numpad7, KEY_KP7},
        {Key::Numpad8, KEY_KP8},
        {Key::Numpad9, KEY_KP9},
        {Key::NumpadDivide, KEY_KPSLASH},
        {Key::NumpadMultiply, KEY_KPASTERISK},
        {Key::NumpadMinus, KEY_KPMINUS},
        {Key::NumpadPlus, KEY_KPPLUS},
        {Key::NumpadEnter, KEY_KPENTER},
        {Key::NumpadDecimal, KEY_KPDOT},

        // Modifiers
        {Key::ShiftLeft, KEY_LEFTSHIFT},
        {Key::ShiftRight, KEY_RIGHTSHIFT},
        {Key::CtrlLeft, KEY_LEFTCTRL},
        {Key::CtrlRight, KEY_RIGHTCTRL},
        {Key::AltLeft, KEY_LEFTALT},
        {Key::AltRight, KEY_RIGHTALT},
        {Key::SuperLeft, KEY_LEFTMETA},
        {Key::SuperRight, KEY_RIGHTMETA},
        {Key::CapsLock, KEY_CAPSLOCK},
        {Key::NumLock, KEY_NUMLOCK},

        // Misc
        {Key::Menu, KEY_MENU},
        {Key::Mute, KEY_MUTE},
        {Key::VolumeDown, KEY_VOLUMEDOWN},
        {Key::VolumeUp, KEY_VOLUMEUP},
        {Key::MediaPlayPause, KEY_PLAYPAUSE},
        {Key::MediaStop, KEY_STOPCD},
        {Key::MediaNext, KEY_NEXTSONG},
        {Key::MediaPrevious, KEY_PREVIOUSSONG},

        // Punctuation / layout-dependent
        {Key::Grave, KEY_GRAVE},
        {Key::Minus, KEY_MINUS},
        {Key::Equal, KEY_EQUAL},
        {Key::LeftBracket, KEY_LEFTBRACE},
        {Key::RightBracket, KEY_RIGHTBRACE},
        {Key::Backslash, KEY_BACKSLASH},
        {Key::Semicolon, KEY_SEMICOLON},
        {Key::Apostrophe, KEY_APOSTROPHE},
        {Key::Comma, KEY_COMMA},
        {Key::Period, KEY_DOT},
        {Key::Slash, KEY_SLASH},
    };
  }

  int linuxKeyCodeFor(Key key) const {
    auto it = keyMap.find(key);
    return (it != keyMap.end()) ? it->second : -1;
  }

  void emit(int type, int code, int val) {
    struct input_event ev{};
    ev.type = static_cast<unsigned short>(type);
    ev.code = static_cast<unsigned short>(code);
    ev.value = val;
    write(fd, &ev, sizeof(ev));
  }

  void sync() { emit(EV_SYN, SYN_REPORT, 0); }

  bool sendKey(Key key, bool down) {
    if (fd < 0)
      return false;

    int code = linuxKeyCodeFor(key);
    if (code < 0)
      return false;

    emit(EV_KEY, code, down ? 1 : 0);
    sync();
    return true;
  }

  void delay() {
    if (keyDelayUs > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(keyDelayUs));
    }
  }
};

Sender::Sender() : m_impl(std::make_unique<Impl>()) {}
Sender::~Sender() = default;
Sender::Sender(Sender &&) noexcept = default;
Sender &Sender::operator=(Sender &&) noexcept = default;

BackendType Sender::type() const { return BackendType::LinuxUInput; }

Capabilities Sender::capabilities() const {
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

bool Sender::isReady() const { return (m_impl && m_impl->fd >= 0); }

bool Sender::requestPermissions() {
  // Can't request at runtime - needs /dev/uinput access (udev rules or root)
  return isReady();
}

bool Sender::keyDown(Key key) {
  if (!m_impl)
    return false;

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
  return m_impl->sendKey(key, true);
}

bool Sender::keyUp(Key key) {
  if (!m_impl)
    return false;

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
  return result;
}

bool Sender::tap(Key key) {
  if (!keyDown(key))
    return false;
  m_impl->delay();
  return keyUp(key);
}

Modifier Sender::activeModifiers() const {
  return m_impl ? m_impl->currentMods : Modifier::None;
}

bool Sender::holdModifier(Modifier mod) {
  bool ok = true;
  if (hasModifier(mod, Modifier::Shift))
    ok &= keyDown(Key::ShiftLeft);
  if (hasModifier(mod, Modifier::Ctrl))
    ok &= keyDown(Key::CtrlLeft);
  if (hasModifier(mod, Modifier::Alt))
    ok &= keyDown(Key::AltLeft);
  if (hasModifier(mod, Modifier::Super))
    ok &= keyDown(Key::SuperLeft);
  return ok;
}

bool Sender::releaseModifier(Modifier mod) {
  bool ok = true;
  if (hasModifier(mod, Modifier::Shift))
    ok &= keyUp(Key::ShiftLeft);
  if (hasModifier(mod, Modifier::Ctrl))
    ok &= keyUp(Key::CtrlLeft);
  if (hasModifier(mod, Modifier::Alt))
    ok &= keyUp(Key::AltLeft);
  if (hasModifier(mod, Modifier::Super))
    ok &= keyUp(Key::SuperLeft);
  return ok;
}

bool Sender::releaseAllModifiers() {
  return releaseModifier(Modifier::Shift | Modifier::Ctrl | Modifier::Alt |
                         Modifier::Super);
}

bool Sender::combo(Modifier mods, Key key) {
  if (!holdModifier(mods))
    return false;
  m_impl->delay();
  bool ok = tap(key);
  m_impl->delay();
  releaseModifier(mods);
  return ok;
}

bool Sender::typeText(const std::u32string & /*text*/) {
  // uinput cannot inject Unicode directly; converting to key events depends
  // on keyboard layout and is outside the scope of this backend.
  return false;
}

bool Sender::typeText(const std::string & /*utf8Text*/) { return false; }

bool Sender::typeCharacter(char32_t /*codepoint*/) { return false; }

void Sender::flush() {
  if (m_impl)
    m_impl->sync();
}

void Sender::setKeyDelay(uint32_t delayUs) {
  if (m_impl)
    m_impl->keyDelayUs = delayUs;
}

} // namespace typr::io

#endif // __linux__ && !BACKEND_USE_X11
