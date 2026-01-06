/**
 * @file keyboard/sender/sender_uinput.cpp
 * @brief Linux/uinput implementation of axidev::io::keyboard::Sender.
 *
 * Uses the Linux uinput subsystem to create a virtual keyboard device and
 * emit EV_KEY events. The implementation is layout-aware and uses xkbcommon
 * to detect the active keyboard layout and translate characters to keycodes
 * where possible. Compiled only when the uinput backend is selected.
 */

#if defined(__linux__) && !defined(BACKEND_USE_X11)

#include <axidev-io/keyboard/sender.hpp>

#include <algorithm>
#include <axidev-io/log.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <xkbcommon/xkbcommon.h>

#include "keyboard/common/linux_keysym.hpp"
#include "keyboard/common/linux_layout.hpp"

namespace axidev::io::keyboard {

/**
 * @internal
 * @brief Pimpl for Sender (uinput backend).
 *
 * Manages the uinput device file descriptor, XKB context/keymap/state, and
 * layout-aware mappings from logical `Key` values and Unicode characters to
 * evdev keycodes. These members and methods are internal implementation
 * details and are not part of the public API.
 */
struct Sender::Impl {
  int fd{-1};
  Modifier currentMods{Modifier::None};
  uint32_t keyDelayUs{1000};

  // Layout-aware mappings: character/Key -> evdev keycode with required
  // modifiers
  std::unordered_map<Key, int> keyMap;
  std::unordered_map<char32_t, KeyMapping> charToKeycode;

  // XKB context for layout detection
  struct xkb_context *xkbCtx{nullptr};
  struct xkb_keymap *xkbKeymap{nullptr};
  struct xkb_state *xkbState{nullptr};

  Impl() {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
      AXIDEV_IO_LOG_ERROR("Sender (uinput): failed to open /dev/uinput: %s",
                          strerror(errno));
      return;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);

#ifdef KEY_MAX
    for (int i = 0; i < KEY_MAX; ++i) {
      ioctl(fd, UI_SET_KEYBIT, i);
    }
#endif

    struct uinput_setup usetup{};
    std::memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    std::strncpy(usetup.name, "Virtual Keyboard", UINPUT_MAX_NAME_SIZE - 1);

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    initXkb();
    initKeyMap();

    AXIDEV_IO_LOG_INFO(
        "Sender (uinput): device initialized fd=%d keymap_entries=%zu "
        "char_entries=%zu",
        fd, keyMap.size(), charToKeycode.size());
  }

  ~Impl() {
    if (xkbState)
      xkb_state_unref(xkbState);
    if (xkbKeymap)
      xkb_keymap_unref(xkbKeymap);
    if (xkbCtx)
      xkb_context_unref(xkbCtx);

    if (fd >= 0) {
      ioctl(fd, UI_DEV_DESTROY);
      close(fd);
      AXIDEV_IO_LOG_INFO("Sender (uinput): device destroyed (fd=%d)", fd);
    }
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&other) noexcept
      : fd(other.fd), currentMods(other.currentMods),
        keyDelayUs(other.keyDelayUs), keyMap(std::move(other.keyMap)),
        charToKeycode(std::move(other.charToKeycode)), xkbCtx(other.xkbCtx),
        xkbKeymap(other.xkbKeymap), xkbState(other.xkbState) {
    other.fd = -1;
    other.xkbCtx = nullptr;
    other.xkbKeymap = nullptr;
    other.xkbState = nullptr;
  }

  Impl &operator=(Impl &&other) noexcept {
    if (this == &other)
      return *this;

    if (xkbState)
      xkb_state_unref(xkbState);
    if (xkbKeymap)
      xkb_keymap_unref(xkbKeymap);
    if (xkbCtx)
      xkb_context_unref(xkbCtx);
    if (fd >= 0) {
      ioctl(fd, UI_DEV_DESTROY);
      close(fd);
    }

    fd = other.fd;
    currentMods = other.currentMods;
    keyDelayUs = other.keyDelayUs;
    keyMap = std::move(other.keyMap);
    charToKeycode = std::move(other.charToKeycode);
    xkbCtx = other.xkbCtx;
    xkbKeymap = other.xkbKeymap;
    xkbState = other.xkbState;

    other.fd = -1;
    other.xkbCtx = nullptr;
    other.xkbKeymap = nullptr;
    other.xkbState = nullptr;
    return *this;
  }

  /**
   * @internal
   * @brief Initialize xkbcommon context, keymap and state for layout
   * translation.
   *
   * This routine attempts to create an `xkb_context`, detect the active
   * keyboard layout (via `detectKeyboardLayout`), and then compile and install
   * a corresponding `xkb_keymap` and `xkb_state`. On any failure the function
   * logs an error and returns without a usable xkb state.
   */
  void initXkb() {
    xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkbCtx) {
      AXIDEV_IO_LOG_ERROR("Sender (uinput): xkb_context_new() failed");
      return;
    }

    // Try to detect the actual keyboard layout (and related XKB fields)
    const auto detected = axidev::io::keyboard::detail::detectXkbRuleNames();

    struct xkb_rule_names names = {nullptr, nullptr, nullptr, nullptr, nullptr};
    std::string dbg;
    if (!detected.rules.empty()) {
      names.rules = detected.rules.c_str();
      dbg += "rules=" + detected.rules + " ";
    }
    if (!detected.model.empty()) {
      names.model = detected.model.c_str();
      dbg += "model=" + detected.model + " ";
    }
    if (!detected.layout.empty()) {
      names.layout = detected.layout.c_str();
      dbg += "layout=" + detected.layout + " ";
    }
    if (!detected.variant.empty()) {
      names.variant = detected.variant.c_str();
      dbg += "variant=" + detected.variant + " ";
    }
    if (!detected.options.empty()) {
      names.options = detected.options.c_str();
      dbg += "options=" + detected.options + " ";
    }
    if (!dbg.empty()) {
      AXIDEV_IO_LOG_INFO("Sender (uinput): xkb names: %s", dbg.c_str());
    }

    xkbKeymap =
        xkb_keymap_new_from_names(xkbCtx, detected.empty() ? nullptr : &names,
                                  XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (!xkbKeymap) {
      AXIDEV_IO_LOG_ERROR(
          "Sender (uinput): xkb_keymap_new_from_names() failed");
      return;
    }

    xkbState = xkb_state_new(xkbKeymap);
    if (!xkbState) {
      AXIDEV_IO_LOG_ERROR("Sender (uinput): xkb_state_new() failed");
      return;
    }

    if (detected.empty()) {
      AXIDEV_IO_LOG_DEBUG(
          "Sender (uinput): could not detect layout, using system default");
    }
  }

  void initKeyMap() {
    const auto linuxMap = detail::initLinuxKeyMap(xkbKeymap, xkbState);
    keyMap = std::move(linuxMap.keyToEvdev);
    charToKeycode = std::move(linuxMap.charToKeycode);

    AXIDEV_IO_LOG_DEBUG(
        "Sender (uinput): initKeyMap populated %zu key entries, %zu char "
        "entries",
        keyMap.size(), charToKeycode.size());
  }

  /**
   * @internal
   * @brief Emit a raw input_event to the uinput device.
   *
   * This helper constructs an `input_event` and writes it directly to the
   * uinput device file descriptor. It is a low-level primitive used by
   * higher-level helpers to synthesize key and synchronization events.
   *
   * @param type Event type (e.g., EV_KEY, EV_SYN).
   * @param code Event code (e.g., key code).
   * @param val Event value (press/release/value).
   */
  void emit(int type, int code, int val) {
    struct input_event ev{};
    ev.type = static_cast<unsigned short>(type);
    ev.code = static_cast<unsigned short>(code);
    ev.value = val;
    if (write(fd, &ev, sizeof(ev)) < 0) {
      AXIDEV_IO_LOG_ERROR("Sender (uinput): write() failed: %s",
                          strerror(errno));
    }
  }

  /**
   * @internal
   * @brief Emit a synchronization report (SYN_REPORT) to flush pending events.
   *
   * This ensures that any previously emitted EV_KEY/EV_REL/EV_ABS events are
   * delivered as an atomic group to the input subsystem.
   */
  void sync() { emit(EV_SYN, SYN_REPORT, 0); }

  /**
   * @internal
   * @brief Send a key event for the given evdev keycode.
   *
   * Wraps `emit(EV_KEY, ...)` and follows with a `sync()` to ensure timely
   * delivery. The function is resilient to a missing device or invalid key
   * code and returns false in those cases.
   *
   * @param evdevCode evdev keycode to send (e.g., KEY_A).
   * @param down true for key press, false for key release.
   * @return true on success, false on failure (e.g. no device or invalid code).
   */
  bool sendKey(int evdevCode, bool down) {
    if (fd < 0 || evdevCode < 0)
      return false;
    emit(EV_KEY, evdevCode, down ? 1 : 0);
    sync();
    return true;
  }

  /**
   * @internal
   * @brief Send a key event for a logical `Key` by looking up its evdev code.
   *
   * Performs a lookup in the layout-aware `keyMap` and forwards to `sendKey`.
   * If no mapping is present, a debug log entry is emitted and the function
   * returns false.
   *
   * @param key Logical `Key` enum to send.
   * @param down true for key press, false for key release.
   * @return true on success, false when mapping is missing or send fails.
   */
  bool sendKeyByKey(Key key, bool down) {
    auto it = keyMap.find(key);
    if (it == keyMap.end()) {
      AXIDEV_IO_LOG_DEBUG("Sender (uinput): no mapping for key=%s",
                          keyToString(key).c_str());
      return false;
    }
    return sendKey(it->second, down);
  }

  /**
   * @internal
   * @brief Type a single Unicode codepoint using layout-derived keycodes.
   *
   * Looks up the provided codepoint in `charToKeycode`, applies the required
   * modifiers (Shift, Ctrl, Alt), emits press/release for the resolved
   * evdev keycode, and synchronizes the event stream.
   *
   * @param cp Unicode codepoint (UTF-32).
   * @return true on success, false when no mapping exists.
   */
  bool typeCodepoint(char32_t cp) {
    auto it = charToKeycode.find(cp);
    if (it == charToKeycode.end()) {
      AXIDEV_IO_LOG_DEBUG("Sender (uinput): no mapping for codepoint U+%04X",
                          static_cast<unsigned>(cp));
      return false;
    }

    const KeyMapping &mapping = it->second;
    int evdevCode = mapping.keycode;
    Modifier requiredMods = mapping.requiredMods;

    // Press required modifiers
    if (hasModifier(requiredMods, Modifier::Shift)) {
      sendKey(KEY_LEFTSHIFT, true);
      delay();
    }
    if (hasModifier(requiredMods, Modifier::Ctrl)) {
      sendKey(KEY_LEFTCTRL, true);
      delay();
    }
    if (hasModifier(requiredMods, Modifier::Alt)) {
      sendKey(KEY_LEFTALT, true);
      delay();
    }

    sendKey(evdevCode, true);
    delay();
    sendKey(evdevCode, false);

    // Release modifiers in reverse order
    if (hasModifier(requiredMods, Modifier::Alt)) {
      delay();
      sendKey(KEY_LEFTALT, false);
    }
    if (hasModifier(requiredMods, Modifier::Ctrl)) {
      delay();
      sendKey(KEY_LEFTCTRL, false);
    }
    if (hasModifier(requiredMods, Modifier::Shift)) {
      delay();
      sendKey(KEY_LEFTSHIFT, false);
    }

    sync();
    return true;
  }

  /**
   * @internal
   * @brief Sleep for the configured key delay interval.
   *
   * Sleeps for `keyDelayUs` microseconds when a non-zero delay is configured.
   * This small helper centralizes the delay logic used by tap/combo and
   * character injection helpers.
   */
  void delay() {
    if (keyDelayUs > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(keyDelayUs));
  }
};

// Public interface implementation
Sender::Sender() : m_impl(std::make_unique<Impl>()) {}
Sender::~Sender() = default;
Sender::Sender(Sender &&) noexcept = default;
Sender &Sender::operator=(Sender &&) noexcept = default;

BackendType Sender::type() const { return BackendType::LinuxUInput; }

Capabilities Sender::capabilities() const {
  return {
      .canInjectKeys = (m_impl && m_impl->fd >= 0),
      .canInjectText = (m_impl && !m_impl->charToKeycode.empty()),
      .canSimulateHID = true,
      .supportsKeyRepeat = true,
      .needsAccessibilityPerm = false,
      .needsInputMonitoringPerm = false,
      .needsUinputAccess = true,
  };
}

bool Sender::isReady() const { return m_impl && m_impl->fd >= 0; }

bool Sender::requestPermissions() { return isReady(); }

// Internal helper to send a raw key event (used by public API)
bool Sender::sendRawKey(Key key, bool down) {
  if (!m_impl)
    return false;

  if (down) {
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
  } else {
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
  }
  return m_impl->sendKeyByKey(key, down);
}

bool Sender::keyDown(KeyWithModifier keyMod) {
  if (!m_impl)
    return false;
  // Press required modifiers first
  if (!holdModifier(keyMod.requiredMods)) {
    return false;
  }
  return sendRawKey(keyMod.key, true);
}

bool Sender::keyUp(KeyWithModifier keyMod) {
  if (!m_impl)
    return false;
  bool result = sendRawKey(keyMod.key, false);
  // Release modifiers that were specified
  releaseModifier(keyMod.requiredMods);
  return result;
}

bool Sender::tap(KeyWithModifier keyMod) {
  if (!m_impl)
    return false;
  // Hold modifiers, tap key, release modifiers
  if (!holdModifier(keyMod.requiredMods)) {
    return false;
  }
  m_impl->delay();
  if (!sendRawKey(keyMod.key, true)) {
    releaseModifier(keyMod.requiredMods);
    return false;
  }
  m_impl->delay();
  bool ok = sendRawKey(keyMod.key, false);
  m_impl->delay();
  releaseModifier(keyMod.requiredMods);
  return ok;
}

Modifier Sender::activeModifiers() const {
  return m_impl ? m_impl->currentMods : Modifier::None;
}

bool Sender::holdModifier(Modifier mod) {
  bool ok = true;
  if (hasModifier(mod, Modifier::Shift))
    ok &= sendRawKey(Key::ShiftLeft, true);
  if (hasModifier(mod, Modifier::Ctrl))
    ok &= sendRawKey(Key::CtrlLeft, true);
  if (hasModifier(mod, Modifier::Alt))
    ok &= sendRawKey(Key::AltLeft, true);
  if (hasModifier(mod, Modifier::Super))
    ok &= sendRawKey(Key::SuperLeft, true);
  return ok;
}

bool Sender::releaseModifier(Modifier mod) {
  bool ok = true;
  if (hasModifier(mod, Modifier::Shift))
    ok &= sendRawKey(Key::ShiftLeft, false);
  if (hasModifier(mod, Modifier::Ctrl))
    ok &= sendRawKey(Key::CtrlLeft, false);
  if (hasModifier(mod, Modifier::Alt))
    ok &= sendRawKey(Key::AltLeft, false);
  if (hasModifier(mod, Modifier::Super))
    ok &= sendRawKey(Key::SuperLeft, false);
  return ok;
}

bool Sender::releaseAllModifiers() {
  return releaseModifier(Modifier::Shift | Modifier::Ctrl | Modifier::Alt |
                         Modifier::Super);
}

bool Sender::typeText(const std::u32string &text) {
  if (!m_impl)
    return false;

  bool allOk = true;
  for (char32_t cp : text) {
    if (!m_impl->typeCodepoint(cp)) {
      allOk = false;
    }
    m_impl->delay();
  }
  return allOk;
}

bool Sender::typeText(const std::string &utf8Text) {
  // Convert UTF-8 to UTF-32
  std::u32string utf32;
  size_t i = 0;
  while (i < utf8Text.size()) {
    char32_t cp = 0;
    auto c = static_cast<unsigned char>(utf8Text[i]);
    if ((c & 0x80) == 0) {
      cp = c;
      i += 1;
    } else if ((c & 0xE0) == 0xC0) {
      cp = (c & 0x1F) << 6;
      if (i + 1 < utf8Text.size())
        cp |= (utf8Text[i + 1] & 0x3F);
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = (c & 0x0F) << 12;
      if (i + 1 < utf8Text.size())
        cp |= (static_cast<unsigned char>(utf8Text[i + 1]) & 0x3F) << 6;
      if (i + 2 < utf8Text.size())
        cp |= (static_cast<unsigned char>(utf8Text[i + 2]) & 0x3F);
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = (c & 0x07) << 18;
      if (i + 1 < utf8Text.size())
        cp |= (static_cast<unsigned char>(utf8Text[i + 1]) & 0x3F) << 12;
      if (i + 2 < utf8Text.size())
        cp |= (static_cast<unsigned char>(utf8Text[i + 2]) & 0x3F) << 6;
      if (i + 3 < utf8Text.size())
        cp |= (static_cast<unsigned char>(utf8Text[i + 3]) & 0x3F);
      i += 4;
    } else {
      i += 1;
      continue;
    }
    utf32.push_back(cp);
  }
  return typeText(utf32);
}

bool Sender::typeCharacter(char32_t codepoint) {
  if (!m_impl)
    return false;
  return m_impl->typeCodepoint(codepoint);
}

void Sender::flush() {
  if (m_impl)
    m_impl->sync();
}

void Sender::setKeyDelay(uint32_t delayUs) {
  if (m_impl)
    m_impl->keyDelayUs = delayUs;
}

} // namespace axidev::io::keyboard

#endif // __linux__ && !BACKEND_USE_X11
