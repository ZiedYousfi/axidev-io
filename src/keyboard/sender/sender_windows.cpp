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

namespace axidev::io::keyboard {

namespace {

/**
 * @internal
 * @brief Return true if a Win32 virtual-key should be treated as an "extended"
 * key when synthesizing input.
 *
 * Certain virtual-key codes require the EXTENDEDKEY flag during input
 * synthesis (e.g., navigation keys, some keypad keys, right-side modifiers).
 * Centralizing the decision here keeps the injection and mapping logic
 * consistent across the Windows backend.
 *
 * @param vk Virtual-key code to test.
 * @return true if the key is considered an extended key; false otherwise.
 */
bool isExtendedKey(WORD vk) {
  switch (vk) {
  case VK_INSERT:
  case VK_DELETE:
  case VK_HOME:
  case VK_END:
  case VK_PRIOR:
  case VK_NEXT:
  case VK_LEFT:
  case VK_RIGHT:
  case VK_UP:
  case VK_DOWN:
  case VK_SNAPSHOT:
  case VK_DIVIDE:
  case VK_NUMLOCK:
  case VK_RCONTROL:
  case VK_RMENU:
  case VK_LWIN:
  case VK_RWIN:
  case VK_APPS:
    return true;
  default:
    return false;
  }
}

} // namespace

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
    // Try to discover printable mappings from the active keyboard layout.
    // Iterate physical scan codes (like macOS) and map them via the layout to
    // their virtual-key and Unicode output. This makes the discovery layout-
    // aware at the physical-key level.
    BYTE keyState[256]{}; // all keys up
    wchar_t buf[4];
    static constexpr int kMaxScanCode = 128;
    for (UINT sc = 0; sc < kMaxScanCode; ++sc) {
      UINT vk = MapVirtualKeyEx(sc, MAPVK_VSC_TO_VK, layout);
      if (vk == 0)
        continue;
      int ret = ToUnicodeEx(vk, sc, keyState, buf,
                            static_cast<int>(sizeof(buf) / sizeof(buf[0])), 0,
                            layout);
      if (ret > 0) {
        wchar_t first = buf[0];
        std::string mappedKeyString;
        if (first == L' ') {
          mappedKeyString = "space";
        } else if (first == L'\t') {
          mappedKeyString = "tab";
        } else if (first == L'\r' || first == L'\n') {
          mappedKeyString = "enter";
        } else if (first < 0x80) {
          mappedKeyString = std::string(1, static_cast<char>(first));
        } else {
          continue;
        }
        Key mapped = stringToKey(mappedKeyString);
        if (mapped != Key::Unknown) {
          if (keyMap.find(mapped) == keyMap.end()) {
            keyMap[mapped] = static_cast<WORD>(vk);
          }
        }
      }
    }

    // Fallback explicit mappings for common non-printable keys / modifiers
    auto setIfMissing = [this](Key k, WORD v) {
      if (this->keyMap.find(k) == this->keyMap.end())
        this->keyMap[k] = v;
    };

    // Common keys
    setIfMissing(Key::Space, VK_SPACE);
    setIfMissing(Key::Enter, VK_RETURN);
    setIfMissing(Key::Tab, VK_TAB);
    setIfMissing(Key::Backspace, VK_BACK);
    setIfMissing(Key::Delete, VK_DELETE);
    setIfMissing(Key::Escape, VK_ESCAPE);
    setIfMissing(Key::Left, VK_LEFT);
    setIfMissing(Key::Right, VK_RIGHT);
    setIfMissing(Key::Up, VK_UP);
    setIfMissing(Key::Down, VK_DOWN);
    setIfMissing(Key::Home, VK_HOME);
    setIfMissing(Key::End, VK_END);
    setIfMissing(Key::PageUp, VK_PRIOR);
    setIfMissing(Key::PageDown, VK_NEXT);

    // Modifiers
    setIfMissing(Key::ShiftLeft, VK_LSHIFT);
    setIfMissing(Key::ShiftRight, VK_RSHIFT);
    setIfMissing(Key::CtrlLeft, VK_LCONTROL);
    setIfMissing(Key::CtrlRight, VK_RCONTROL);
    setIfMissing(Key::AltLeft, VK_LMENU);
    setIfMissing(Key::AltRight, VK_RMENU);
    setIfMissing(Key::SuperLeft, VK_LWIN);
    setIfMissing(Key::SuperRight, VK_RWIN);
    setIfMissing(Key::CapsLock, VK_CAPITAL);

    // Function keys
    setIfMissing(Key::F1, VK_F1);
    setIfMissing(Key::F2, VK_F2);
    setIfMissing(Key::F3, VK_F3);
    setIfMissing(Key::F4, VK_F4);
    setIfMissing(Key::F5, VK_F5);
    setIfMissing(Key::F6, VK_F6);
    setIfMissing(Key::F7, VK_F7);
    setIfMissing(Key::F8, VK_F8);
    setIfMissing(Key::F9, VK_F9);
    setIfMissing(Key::F10, VK_F10);
    setIfMissing(Key::F11, VK_F11);
    setIfMissing(Key::F12, VK_F12);
    setIfMissing(Key::F13, VK_F13);
    setIfMissing(Key::F14, VK_F14);
    setIfMissing(Key::F15, VK_F15);
    setIfMissing(Key::F16, VK_F16);
    setIfMissing(Key::F17, VK_F17);
    setIfMissing(Key::F18, VK_F18);
    setIfMissing(Key::F19, VK_F19);
    setIfMissing(Key::F20, VK_F20);

    // Top-row numbers (unshifted)
    // Ensure top-row numeric keys that may be layout-dependent still have a
    // sensible fallback mapping so callers using Key::NumX can inject digits.
    setIfMissing(Key::Num0, VK_NUMPAD0);
    setIfMissing(Key::Num1, VK_NUMPAD1);
    setIfMissing(Key::Num2, VK_NUMPAD2);
    setIfMissing(Key::Num3, VK_NUMPAD3);
    setIfMissing(Key::Num4, VK_NUMPAD4);
    setIfMissing(Key::Num5, VK_NUMPAD5);
    setIfMissing(Key::Num6, VK_NUMPAD6);
    setIfMissing(Key::Num7, VK_NUMPAD7);
    setIfMissing(Key::Num8, VK_NUMPAD8);
    setIfMissing(Key::Num9, VK_NUMPAD9);

    // Numpad
    setIfMissing(Key::Numpad0, VK_NUMPAD0);
    setIfMissing(Key::Numpad1, VK_NUMPAD1);
    setIfMissing(Key::Numpad2, VK_NUMPAD2);
    setIfMissing(Key::Numpad3, VK_NUMPAD3);
    setIfMissing(Key::Numpad4, VK_NUMPAD4);
    setIfMissing(Key::Numpad5, VK_NUMPAD5);
    setIfMissing(Key::Numpad6, VK_NUMPAD6);
    setIfMissing(Key::Numpad7, VK_NUMPAD7);
    setIfMissing(Key::Numpad8, VK_NUMPAD8);
    setIfMissing(Key::Numpad9, VK_NUMPAD9);
    setIfMissing(Key::NumpadDivide, VK_DIVIDE);
    setIfMissing(Key::NumpadMultiply, VK_MULTIPLY);
    setIfMissing(Key::NumpadMinus, VK_SUBTRACT);
    setIfMissing(Key::NumpadPlus, VK_ADD);
    setIfMissing(Key::NumpadEnter, VK_RETURN);
    setIfMissing(Key::NumpadDecimal, VK_DECIMAL);

    // Misc
    setIfMissing(Key::Menu, VK_APPS);
    setIfMissing(Key::Mute, VK_VOLUME_MUTE);
    setIfMissing(Key::VolumeDown, VK_VOLUME_DOWN);
    setIfMissing(Key::VolumeUp, VK_VOLUME_UP);
    setIfMissing(Key::MediaPlayPause, VK_MEDIA_PLAY_PAUSE);
    setIfMissing(Key::MediaStop, VK_MEDIA_STOP);
    setIfMissing(Key::MediaNext, VK_MEDIA_NEXT_TRACK);
    setIfMissing(Key::MediaPrevious, VK_MEDIA_PREV_TRACK);

    // Punctuation (OEM)
    setIfMissing(Key::Grave, VK_OEM_3);
    setIfMissing(Key::Minus, VK_OEM_MINUS);
    setIfMissing(Key::Equal, VK_OEM_PLUS);
    setIfMissing(Key::LeftBracket, VK_OEM_4);
    setIfMissing(Key::RightBracket, VK_OEM_6);
    setIfMissing(Key::Backslash, VK_OEM_5);
    setIfMissing(Key::Semicolon, VK_OEM_1);
    setIfMissing(Key::Apostrophe, VK_OEM_7);
    setIfMissing(Key::Comma, VK_OEM_COMMA);
    setIfMissing(Key::Period, VK_OEM_PERIOD);
    setIfMissing(Key::Slash, VK_OEM_2);
    AXIDEV_IO_LOG_DEBUG("Sender (Windows): initKeyMap populated %zu entries",
                      keyMap.size());
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

    if (isExtendedKey(vk)) {
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

AXIDEV_IO_API bool Sender::keyDown(Key key) {
  AXIDEV_IO_LOG_DEBUG("Sender::keyDown %s", keyToString(key).c_str());
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
  return m_impl->sendKey(key, true);
}

AXIDEV_IO_API bool Sender::keyUp(Key key) {
  AXIDEV_IO_LOG_DEBUG("Sender::keyUp %s", keyToString(key).c_str());
  bool result = m_impl->sendKey(key, false);
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
  return result;
}

AXIDEV_IO_API bool Sender::tap(Key key) {
  AXIDEV_IO_LOG_DEBUG("Sender::tap %s", keyToString(key).c_str());
  if (!keyDown(key))
    return false;
  m_impl->delay();
  return keyUp(key);
}

AXIDEV_IO_API Modifier Sender::activeModifiers() const {
  return m_impl->currentMods;
}

AXIDEV_IO_API bool Sender::holdModifier(Modifier mod) {
  bool allModifiersPressed = true;
  if (hasModifier(mod, Modifier::Shift)) {
    allModifiersPressed &= keyDown(Key::ShiftLeft);
  }
  if (hasModifier(mod, Modifier::Ctrl)) {
    allModifiersPressed &= keyDown(Key::CtrlLeft);
  }
  if (hasModifier(mod, Modifier::Alt)) {
    allModifiersPressed &= keyDown(Key::AltLeft);
  }
  if (hasModifier(mod, Modifier::Super)) {
    allModifiersPressed &= keyDown(Key::SuperLeft);
  }
  return allModifiersPressed;
}

AXIDEV_IO_API bool Sender::releaseModifier(Modifier mod) {
  bool allModifiersReleased = true;
  if (hasModifier(mod, Modifier::Shift)) {
    allModifiersReleased &= keyUp(Key::ShiftLeft);
  }
  if (hasModifier(mod, Modifier::Ctrl)) {
    allModifiersReleased &= keyUp(Key::CtrlLeft);
  }
  if (hasModifier(mod, Modifier::Alt)) {
    allModifiersReleased &= keyUp(Key::AltLeft);
  }
  if (hasModifier(mod, Modifier::Super)) {
    allModifiersReleased &= keyUp(Key::SuperLeft);
  }
  return allModifiersReleased;
}

AXIDEV_IO_API bool Sender::releaseAllModifiers() {
  return releaseModifier(Modifier::Shift | Modifier::Ctrl | Modifier::Alt |
                         Modifier::Super);
}

AXIDEV_IO_API bool Sender::combo(Modifier mods, Key key) {
  if (!holdModifier(mods))
    return false;
  m_impl->delay();
  bool ok = tap(key);
  m_impl->delay();
  releaseModifier(mods);
  return ok;
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
