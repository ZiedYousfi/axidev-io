/**
 * @file keyboard/sender/sender_windows.cpp
 * @brief Windows implementation of axidev::io::keyboard::Sender.
 *
 * Implements keyboard input injection using Win32 APIs (SendInput / keybd
 * events). The implementation is layout-aware and discovers virtual-key
 * mappings from the current keyboard layout.
 */

#ifdef _WIN32
#include <Windows.h>
#include <axidev-io/keyboard/sender.hpp>
#include <axidev-io/log.hpp>
#include <vector>
#include <unordered_map>

#include "keyboard/common/windows_keymap.hpp"

namespace axidev::io::keyboard {

/**
 * @internal
 * @brief PIMPL implementation for the Windows Sender backend.
 *
 * Contains platform-specific state used for input injection on Windows,
 * including discovered mappings (Key -> VK), the active keyboard layout
 * handle (HKL), and the tracked modifier state. These details are internal
 * implementation concerns and are not part of the public API.
 */
struct Sender::Impl {
  Modifier currentMods{Modifier::None};
  uint32_t keyDelayUs{1000}; // 1ms default
  bool ready{true};
  HKL layout{nullptr};
  std::unordered_map<Key, WORD> keyMap;
  std::unordered_map<char32_t, KeyMapping> charToKeycode;

  Impl() : layout(GetKeyboardLayout(0)) {
    initKeyMap();
    AXIDEV_IO_LOG_INFO("Sender (Windows): Impl created; ready=%u",
                     static_cast<unsigned>(ready));
  }

  /**
   * @internal
   * @brief Build layout-aware mappings for printable characters and logical
   * keys.
   *
   * Scans physical scan codes against the active keyboard layout to discover
   * which virtual-key codes produce which Unicode characters and which logical
   * `Key` values. Populates `keyMap` (Key -> VK) and `charToKeycode` (codepoint
   * -> (VK, requiresShift)). Falls back to sensible defaults when detailed
   * layout information is not available.
   */
  void initKeyMap() {
    auto km = ::axidev::io::keyboard::detail::initWindowsKeyMap(layout);
    keyMap = std::move(km.keyToVk);
    charToKeycode = std::move(km.charToKeycode);
    AXIDEV_IO_LOG_DEBUG("Sender (Windows): initKeyMap populated %zu entries",
                      keyMap.size());
    AXIDEV_IO_LOG_DEBUG("Sender (Windows): charToKeycode populated %zu entries",
                      charToKeycode.size());
  }

  /**
   * @internal
   * @brief Return the Win32 virtual-key code (VK) for a logical `Key`.
   *
   * Looks up the provided `Key` in the internal `keyMap` and returns the
   * associated Win32 `WORD` virtual-key code. If no mapping exists, returns 0.
   *
   * This helper is internal to the Windows sender implementation and is used
   * by the event synthesis routines when translating logical `Key` values
   * to platform-specific virtual-key codes.
   *
   * @param key Logical `Key` to translate.
   * @return WORD Virtual-key code, or 0 if no mapping is present.
   */
  WORD winVkFor(Key key) const {
    auto it = keyMap.find(key);
    return (it != keyMap.end()) ? it->second : 0;
  }

  /**
   * @internal
   * @brief Synthesize a keyboard event for the given logical `Key`.
   *
   * This routine converts the logical `Key` to a Win32 virtual-key code using
   * `winVkFor` and synthesizes a key press or release using `SendInput`.
   * It sets the appropriate scancode and, when necessary, the extended-key
   * flag for keys that require it.
   *
   * @param key Logical `Key` to send.
   * @param down True to send a key-down event; false to send a key-up event.
   * @return true on success (SendInput reports events were queued), false on
   *         failure or when the key has no known mapping.
   */
  bool sendKey(Key key, bool down) {
    WORD vk = winVkFor(key);
    if (vk == 0) {
      AXIDEV_IO_LOG_DEBUG("Sender (Windows): no mapping for key=%s",
                        keyToString(key).c_str());
      return false;
    }

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = KEYEVENTF_SCANCODE;

    if (::axidev::io::keyboard::detail::isWindowsExtendedKey(vk)) {
      input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (!down) {
      input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    BOOL ok = SendInput(1, &input, sizeof(INPUT)) > 0;
    if (!ok) {
      AXIDEV_IO_LOG_ERROR("Sender (Windows): SendInput failed for vk=%u key=%s",
                        static_cast<unsigned>(vk), keyToString(key).c_str());
    } else {
      AXIDEV_IO_LOG_DEBUG("Sender (Windows): sendKey vk=%u key=%s down=%u",
                        static_cast<unsigned>(vk), keyToString(key).c_str(),
                        static_cast<unsigned>(down));
    }
    return ok;
  }

  /**
   * @internal
   * @brief Type a sequence of Unicode codepoints using Win32 synthetic events.
   *
   * This helper converts each UTF-32 codepoint to UTF-16 (handling surrogate
   * pairs when necessary) and synthesizes the corresponding keyboard input
   * events via `SendInput`. It assembles `INPUT` structures for each codepoint
   * including the key-down / key-up pairs required to generate Unicode
   * characters through the system's input pipeline.
   *
   * @param text UTF-32 string containing codepoints to type.
   * @return true on success; false if an error occurred while sending input.
   */
  bool typeUnicode(const std::u32string &text) {
    AXIDEV_IO_LOG_DEBUG("Sender::typeUnicode called with %zu codepoints",
                      text.size());
    if (text.empty())
      return true;

    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 4); // Worst case: surrogate pairs + up/down

    for (char32_t cp : text) {
      // Convert to UTF-16
      wchar_t utf16[2];
      int len = 0;

      if (cp <= 0xFFFF) {
        utf16[0] = static_cast<wchar_t>(cp);
        len = 1;
      } else if (cp <= 0x10FFFF) {
        cp -= 0x10000;
        utf16[0] = static_cast<wchar_t>(0xD800 | (cp >> 10));
        utf16[1] = static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
        len = 2;
      } else {
        continue; // Invalid codepoint
      }

      for (int i = 0; i < len; ++i) {
        INPUT down{}, up{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = utf16[i];
        down.ki.dwFlags = KEYEVENTF_UNICODE;

        up = down;
        up.ki.dwFlags |= KEYEVENTF_KEYUP;

        inputs.push_back(down);
        inputs.push_back(up);
      }
    }

    return SendInput(static_cast<UINT>(inputs.size()), inputs.data(),
                     sizeof(INPUT)) > 0;
  }

  void delay() {
    if (keyDelayUs > 0) {
      // Use Windows Sleep API to avoid nanosleepe64 dependency issues
      // with MinGW on older Windows versions. Convert microseconds to
      // milliseconds.
      unsigned long ms = (keyDelayUs + 999) / 1000; // Round up
      Sleep(ms);
    }
  }
};

AXIDEV_IO_API Sender::Sender() : m_impl(std::make_unique<Impl>()) {
  AXIDEV_IO_LOG_INFO("Sender (Windows): constructed, ready=%u",
                   static_cast<unsigned>(isReady()));
}
AXIDEV_IO_API Sender::~Sender() = default;
AXIDEV_IO_API Sender::Sender(Sender &&) noexcept = default;
AXIDEV_IO_API Sender &Sender::operator=(Sender &&) noexcept = default;

AXIDEV_IO_API BackendType Sender::type() const {
  AXIDEV_IO_LOG_DEBUG("Sender::type() -> Windows");
  return BackendType::Windows;
}

AXIDEV_IO_API Capabilities Sender::capabilities() const {
  AXIDEV_IO_LOG_DEBUG("Sender::capabilities (Windows) called");
  return {
      .canInjectKeys = m_impl->ready,
      .canInjectText = m_impl->ready,
      .canSimulateHID =
          false, // Align with macOS: treat platform events as not true HID
      .supportsKeyRepeat = false,
      .needsAccessibilityPerm = false,
      .needsInputMonitoringPerm = false,
      .needsUinputAccess = false,
  };
}

AXIDEV_IO_API bool Sender::isReady() const { return m_impl->ready; }
AXIDEV_IO_API bool Sender::requestPermissions() { return true; }

// Internal helper to send a raw key event (used by public API)
AXIDEV_IO_API bool Sender::sendRawKey(Key key, bool down) {
  if (down) {
    // Update modifier state when a modifier key is pressed
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
    // Update modifier state when a modifier key is released
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
  return m_impl->sendKey(key, down);
}

AXIDEV_IO_API bool Sender::keyDown(KeyWithModifier keyMod) {
  AXIDEV_IO_LOG_DEBUG(
      "Sender::keyDown %s",
      keyToStringWithModifier(keyMod.key, keyMod.requiredMods).c_str());
  // Press required modifiers first
  if (!holdModifier(keyMod.requiredMods)) {
    return false;
  }
  return sendRawKey(keyMod.key, true);
}

AXIDEV_IO_API bool Sender::keyUp(KeyWithModifier keyMod) {
  AXIDEV_IO_LOG_DEBUG(
      "Sender::keyUp %s",
      keyToStringWithModifier(keyMod.key, keyMod.requiredMods).c_str());
  bool result = sendRawKey(keyMod.key, false);
  // Release modifiers that were specified
  releaseModifier(keyMod.requiredMods);
  return result;
}

AXIDEV_IO_API bool Sender::tap(KeyWithModifier keyMod) {
  AXIDEV_IO_LOG_DEBUG(
      "Sender::tap %s",
      keyToStringWithModifier(keyMod.key, keyMod.requiredMods).c_str());
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

AXIDEV_IO_API Modifier Sender::activeModifiers() const {
  return m_impl->currentMods;
}

AXIDEV_IO_API bool Sender::holdModifier(Modifier mod) {
  bool allModifiersPressed = true;
  if (hasModifier(mod, Modifier::Shift)) {
    allModifiersPressed &= sendRawKey(Key::ShiftLeft, true);
  }
  if (hasModifier(mod, Modifier::Ctrl)) {
    allModifiersPressed &= sendRawKey(Key::CtrlLeft, true);
  }
  if (hasModifier(mod, Modifier::Alt)) {
    allModifiersPressed &= sendRawKey(Key::AltLeft, true);
  }
  if (hasModifier(mod, Modifier::Super)) {
    allModifiersPressed &= sendRawKey(Key::SuperLeft, true);
  }
  return allModifiersPressed;
}

AXIDEV_IO_API bool Sender::releaseModifier(Modifier mod) {
  bool allModifiersReleased = true;
  if (hasModifier(mod, Modifier::Shift)) {
    allModifiersReleased &= sendRawKey(Key::ShiftLeft, false);
  }
  if (hasModifier(mod, Modifier::Ctrl)) {
    allModifiersReleased &= sendRawKey(Key::CtrlLeft, false);
  }
  if (hasModifier(mod, Modifier::Alt)) {
    allModifiersReleased &= sendRawKey(Key::AltLeft, false);
  }
  if (hasModifier(mod, Modifier::Super)) {
    allModifiersReleased &= sendRawKey(Key::SuperLeft, false);
  }
  return allModifiersReleased;
}

AXIDEV_IO_API bool Sender::releaseAllModifiers() {
  return releaseModifier(Modifier::Shift | Modifier::Ctrl | Modifier::Alt |
                         Modifier::Super);
}

AXIDEV_IO_API bool Sender::typeText(const std::u32string &text) {
  AXIDEV_IO_LOG_DEBUG("Sender::typeText (utf32) called with %zu codepoints",
                    text.size());
  return m_impl->typeUnicode(text);
}

AXIDEV_IO_API bool Sender::typeText(const std::string &utf8Text) {
  // Convert UTF-8 to UTF-32
  std::u32string utf32;
  size_t i = 0;
  while (i < utf8Text.size()) {
    char32_t cp = 0;
    unsigned char c = utf8Text[i];

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
        cp |= (utf8Text[i + 1] & 0x3F) << 6;
      if (i + 2 < utf8Text.size())
        cp |= (utf8Text[i + 2] & 0x3F);
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = (c & 0x07) << 18;
      if (i + 1 < utf8Text.size())
        cp |= (utf8Text[i + 1] & 0x3F) << 12;
      if (i + 2 < utf8Text.size())
        cp |= (utf8Text[i + 2] & 0x3F) << 6;
      if (i + 3 < utf8Text.size())
        cp |= (utf8Text[i + 3] & 0x3F);
      i += 4;
    } else {
      i += 1;
      continue;
    }

    utf32.push_back(cp);
  }
  return typeText(utf32);
}

AXIDEV_IO_API bool Sender::typeCharacter(char32_t codepoint) {
  return typeText(std::u32string(1, codepoint));
}

AXIDEV_IO_API void Sender::flush() {
  // Windows SendInput is synchronous, nothing to flush
}

AXIDEV_IO_API void Sender::setKeyDelay(uint32_t delayUs) {
  m_impl->keyDelayUs = delayUs;
}

} // namespace axidev::io::keyboard

#endif // _WIN32
