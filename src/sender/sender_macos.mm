#ifdef __APPLE__

#include <typr-io/sender.hpp>

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#include <chrono>
#include <thread>
#include <unordered_map>

namespace typr::io {

namespace {

// Key map moved into Impl and initialized per-instance.

CGEventFlags modifierToFlags(Modifier mod) {
  CGEventFlags flags = 0;
  if (hasModifier(mod, Modifier::Shift)) {
    flags |= kCGEventFlagMaskShift;
  }
  if (hasModifier(mod, Modifier::Ctrl)) {
    flags |= kCGEventFlagMaskControl;
  }
  if (hasModifier(mod, Modifier::Alt)) {
    flags |= kCGEventFlagMaskAlternate;
  }
  if (hasModifier(mod, Modifier::Super)) {
    flags |= kCGEventFlagMaskCommand;
  }
  if (hasModifier(mod, Modifier::CapsLock)) {
    flags |= kCGEventFlagMaskAlphaShift;
  }
  return flags;
}

} // namespace

struct Sender::Impl {
  // Unicode and UTF-16 surrogate pair constants
  static constexpr char32_t kUnicodeMaxBMP = 0xFFFF;
  static constexpr char32_t kUnicodeMax = 0x10FFFF;
  static constexpr char32_t kUnicodeSurrogateOffset = 0x10000;
  static constexpr char32_t kUnicodeHighSurrogateBase = 0xD800;
  static constexpr char32_t kUnicodeLowSurrogateBase = 0xDC00;
  static constexpr char32_t kUnicodeSurrogateMask = 0x3FF;
  CGEventSourceRef eventSource{nullptr};
  Modifier currentMods{Modifier::None};
  static constexpr uint32_t kDefaultKeyDelayUs = 1000;
  uint32_t keyDelayUs{kDefaultKeyDelayUs};
  bool ready{false};

  // Map from our Key enum to macOS keycodes
  std::unordered_map<Key, CGKeyCode> keyMap;

  Impl()
      : eventSource(CGEventSourceCreate(kCGEventSourceStateHIDSystemState)),
        ready(AXIsProcessTrustedWithOptions(nullptr) != 0U) {
    initKeyMap();
  }

  ~Impl() {
    if (eventSource != nullptr) {
      CFRelease(eventSource);
    }
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;

  Impl(Impl &&other) noexcept
      : eventSource(other.eventSource), currentMods(other.currentMods),
        keyDelayUs(other.keyDelayUs), ready(other.ready),
        keyMap(std::move(other.keyMap)) {
    other.eventSource = nullptr;
    other.currentMods = Modifier::None;
    other.keyDelayUs = 0;
    other.ready = false;
  }

  Impl &operator=(Impl &&other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (eventSource != nullptr) {
      CFRelease(eventSource);
    }
    eventSource = other.eventSource;
    currentMods = other.currentMods;
    keyDelayUs = other.keyDelayUs;
    ready = other.ready;
    keyMap = std::move(other.keyMap);

    other.eventSource = nullptr;
    other.currentMods = Modifier::None;
    other.keyDelayUs = 0;
    other.ready = false;

    return *this;
  }

  void initKeyMap() {
    TISInputSourceRef currentKeyboard = TISCopyCurrentKeyboardInputSource();
    if (currentKeyboard == nullptr) {
      return;
    }

    const auto *layoutData = static_cast<CFDataRef>(TISGetInputSourceProperty(
        currentKeyboard, kTISPropertyUnicodeKeyLayoutData));
    if (layoutData == nullptr) {
      CFRelease(currentKeyboard);
      return;
    }

    const auto *keyboardLayout = static_cast<const UCKeyboardLayout *>(
        static_cast<const void *>(CFDataGetBytePtr(layoutData)));

    UInt32 keysDown = 0;
    static constexpr int kMaxKeyCode = 128;
    static constexpr size_t kUnicodeStringSize = 4;
    for (int keyCode = 0; keyCode < kMaxKeyCode; keyCode++) {
      std::array<UniChar, kUnicodeStringSize> unicodeString{};
      UniCharCount actualStringLength = 0;

      OSStatus status = UCKeyTranslate(
          keyboardLayout, keyCode, kUCKeyActionDisplay,
          0, // no modifier
          LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit, &keysDown,
          static_cast<UInt32>(unicodeString.size()), &actualStringLength,
          static_cast<UniChar *>(unicodeString.data()));

      if (status == noErr && actualStringLength > 0) {
        UniChar firstUnicodeChar = unicodeString[0];
        std::string mappedKeyString;
        static constexpr UniChar kAsciiSpace = ' ';
        static constexpr UniChar kAsciiTab = '\t';
        static constexpr UniChar kAsciiCR = '\r';
        static constexpr UniChar kAsciiLF = '\n';
        static constexpr UniChar kAsciiMax = 0x80;
        if (firstUnicodeChar == kAsciiSpace) {
          mappedKeyString = "space"; // map space to canonical name
        } else if (firstUnicodeChar == kAsciiTab) {
          mappedKeyString = "tab";
        } else if (firstUnicodeChar == kAsciiCR ||
                   firstUnicodeChar == kAsciiLF) {
          mappedKeyString = "enter";
        } else if (firstUnicodeChar < kAsciiMax) {
          mappedKeyString = std::string(1, static_cast<char>(firstUnicodeChar));
        } else {
          // non-ASCII mapping isn't covered by `Key` enum; skip
          continue;
        }

        Key mappedKeyEnum = stringToKey(mappedKeyString);
        if (mappedKeyEnum != Key::Unknown) {
          if (keyMap.find(mappedKeyEnum) == keyMap.end()) {
            keyMap[mappedKeyEnum] = static_cast<CGKeyCode>(keyCode);
          }
        }
      }
    }

    // Fallback explicit mappings for common non-printable keys / modifiers
    auto setIfMissing = [this](Key keyToSet, CGKeyCode code) {
      if (this->keyMap.find(keyToSet) == this->keyMap.end()) {
        this->keyMap[keyToSet] = code;
      }
    };

    // Common keys
    setIfMissing(Key::Space, kVK_Space);
    setIfMissing(Key::Enter, kVK_Return);
    setIfMissing(Key::Tab, kVK_Tab);
    setIfMissing(Key::Backspace, kVK_Delete); // Backspace key
    setIfMissing(Key::Delete, kVK_ForwardDelete);
    setIfMissing(Key::Escape, kVK_Escape);
    setIfMissing(Key::Left, kVK_LeftArrow);
    setIfMissing(Key::Right, kVK_RightArrow);
    setIfMissing(Key::Up, kVK_UpArrow);
    setIfMissing(Key::Down, kVK_DownArrow);
    setIfMissing(Key::Home, kVK_Home);
    setIfMissing(Key::End, kVK_End);
    setIfMissing(Key::PageUp, kVK_PageUp);
    setIfMissing(Key::PageDown, kVK_PageDown);

    // Modifiers
    setIfMissing(Key::ShiftLeft, kVK_Shift);
    setIfMissing(Key::ShiftRight, kVK_RightShift);
    setIfMissing(Key::CtrlLeft, kVK_Control);
    setIfMissing(Key::CtrlRight, kVK_RightControl);
    setIfMissing(Key::AltLeft, kVK_Option);
    setIfMissing(Key::AltRight, kVK_RightOption);
    setIfMissing(Key::SuperLeft, kVK_Command);
    setIfMissing(Key::SuperRight, kVK_RightCommand);
    setIfMissing(Key::CapsLock, kVK_CapsLock);

    // Function keys
    setIfMissing(Key::F1, kVK_F1);
    setIfMissing(Key::F2, kVK_F2);
    setIfMissing(Key::F3, kVK_F3);
    setIfMissing(Key::F4, kVK_F4);
    setIfMissing(Key::F5, kVK_F5);
    setIfMissing(Key::F6, kVK_F6);
    setIfMissing(Key::F7, kVK_F7);
    setIfMissing(Key::F8, kVK_F8);
    setIfMissing(Key::F9, kVK_F9);
    setIfMissing(Key::F10, kVK_F10);
    setIfMissing(Key::F11, kVK_F11);
    setIfMissing(Key::F12, kVK_F12);
    setIfMissing(Key::F13, kVK_F13);
    setIfMissing(Key::F14, kVK_F14);
    setIfMissing(Key::F15, kVK_F15);
    setIfMissing(Key::F16, kVK_F16);
    setIfMissing(Key::F17, kVK_F17);
    setIfMissing(Key::F18, kVK_F18);
    setIfMissing(Key::F19, kVK_F19);
    setIfMissing(Key::F20, kVK_F20);

    // Numpad
    setIfMissing(Key::Numpad0, kVK_ANSI_Keypad0);
    setIfMissing(Key::Numpad1, kVK_ANSI_Keypad1);
    setIfMissing(Key::Numpad2, kVK_ANSI_Keypad2);
    setIfMissing(Key::Numpad3, kVK_ANSI_Keypad3);
    setIfMissing(Key::Numpad4, kVK_ANSI_Keypad4);
    setIfMissing(Key::Numpad5, kVK_ANSI_Keypad5);
    setIfMissing(Key::Numpad6, kVK_ANSI_Keypad6);
    setIfMissing(Key::Numpad7, kVK_ANSI_Keypad7);
    setIfMissing(Key::Numpad8, kVK_ANSI_Keypad8);
    setIfMissing(Key::Numpad9, kVK_ANSI_Keypad9);
    setIfMissing(Key::NumpadDivide, kVK_ANSI_KeypadDivide);
    setIfMissing(Key::NumpadMultiply, kVK_ANSI_KeypadMultiply);
    setIfMissing(Key::NumpadMinus, kVK_ANSI_KeypadMinus);
    setIfMissing(Key::NumpadPlus, kVK_ANSI_KeypadPlus);
    setIfMissing(Key::NumpadEnter, kVK_ANSI_KeypadEnter);
    setIfMissing(Key::NumpadDecimal, kVK_ANSI_KeypadDecimal);

    // Punctuation (ANSI)
    setIfMissing(Key::Grave, kVK_ANSI_Grave);
    setIfMissing(Key::Minus, kVK_ANSI_Minus);
    setIfMissing(Key::Equal, kVK_ANSI_Equal);
    setIfMissing(Key::LeftBracket, kVK_ANSI_LeftBracket);
    setIfMissing(Key::RightBracket, kVK_ANSI_RightBracket);
    setIfMissing(Key::Backslash, kVK_ANSI_Backslash);
    setIfMissing(Key::Semicolon, kVK_ANSI_Semicolon);
    setIfMissing(Key::Apostrophe, kVK_ANSI_Quote);
    setIfMissing(Key::Comma, kVK_ANSI_Comma);
    setIfMissing(Key::Period, kVK_ANSI_Period);
    setIfMissing(Key::Slash, kVK_ANSI_Slash);

    CFRelease(currentKeyboard);
  }

  [[nodiscard]] CGKeyCode macKeyCodeFor(Key key) const {
    auto keyMapIt = keyMap.find(key);
    static constexpr CGKeyCode kInvalidKeyCode = UINT16_MAX;
    return (keyMapIt != keyMap.end()) ? keyMapIt->second : kInvalidKeyCode;
  }

  bool sendKey(Key key, bool down) const {
    CGKeyCode keyCode = macKeyCodeFor(key);
    static constexpr CGKeyCode kInvalidKeyCode = UINT16_MAX;
    if (keyCode == kInvalidKeyCode) {
      return false;
    }

    CGEventRef event = CGEventCreateKeyboardEvent(eventSource, keyCode, down);
    if (event == nullptr) {
      return false;
    }

    // Apply current modifier state
    CGEventSetFlags(event, modifierToFlags(currentMods));
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return true;
  }

  [[nodiscard]] bool typeUnicode(const std::u32string &text) const {
    if (text.empty()) {
      return true;
    }

    // Convert to UTF-16
    std::vector<UniChar> utf16;
    for (char32_t codepoint : text) {
      if (codepoint <= kUnicodeMaxBMP) {
        utf16.push_back(static_cast<UniChar>(codepoint));
      } else if (codepoint <= kUnicodeMax) {
        char32_t codepointTmp = codepoint - kUnicodeSurrogateOffset;
        utf16.push_back(
            static_cast<UniChar>(kUnicodeHighSurrogateBase |
                                 (static_cast<uint32_t>(codepointTmp) >> 10)));
        utf16.push_back(static_cast<UniChar>(
            kUnicodeLowSurrogateBase |
            (static_cast<uint32_t>(codepointTmp) & kUnicodeSurrogateMask)));
      }
    }

    // macOS limit: 20 characters per event
    static constexpr size_t kMaxCharsPerEvent = 20;

    for (size_t utf16Index = 0; utf16Index < utf16.size();
         utf16Index += kMaxCharsPerEvent) {
      size_t chunkLength =
          std::min(kMaxCharsPerEvent, utf16.size() - utf16Index);

      CGEventRef eventDown = CGEventCreateKeyboardEvent(eventSource, 0, true);
      CGEventRef eventUp = CGEventCreateKeyboardEvent(eventSource, 0, false);
      if ((eventDown == nullptr) || (eventUp == nullptr)) {
        if (eventDown != nullptr) {
          CFRelease(eventDown);
        }
        if (eventUp != nullptr) {
          CFRelease(eventUp);
        }
        return false;
      }

      CGEventKeyboardSetUnicodeString(eventDown, chunkLength,
                                      &utf16[utf16Index]);
      CGEventKeyboardSetUnicodeString(eventUp, chunkLength, &utf16[utf16Index]);

      CGEventPost(kCGHIDEventTap, eventDown);
      CGEventPost(kCGHIDEventTap, eventUp);

      CFRelease(eventDown);
      CFRelease(eventUp);
    }
    return true;
  }

  void delay() const {
    if (keyDelayUs > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(keyDelayUs));
    }
  }
};

Sender::Sender() : m_impl(std::make_unique<Impl>()) {}
Sender::~Sender() = default;
Sender::Sender(Sender &&) noexcept = default;
Sender &Sender::operator=(Sender &&) noexcept = default;

BackendType Sender::type() const { return BackendType::MacOS; }

Capabilities Sender::capabilities() const {
  return {
      .canInjectKeys = m_impl->ready,
      .canInjectText = m_impl->ready,
      .canSimulateHID = false, // macOS CGEvent is not true HID
      .supportsKeyRepeat = true,
      .needsAccessibilityPerm = true,
      .needsInputMonitoringPerm = false,
      .needsUinputAccess = false,
  };
}

bool Sender::isReady() const { return m_impl->ready; }

bool Sender::requestPermissions() {
  NSDictionary *opts =
      @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
  m_impl->ready =
      (AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts) != 0U);
  return m_impl->ready;
}

bool Sender::keyDown(Key key) {
  // Update modifier state if pressing a modifier
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
  bool result = m_impl->sendKey(key, false);
  // Update modifier state if releasing a modifier
  switch (key) {
  case Key::ShiftLeft:
  case Key::ShiftRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<unsigned int>(m_impl->currentMods) &
                              ~static_cast<unsigned int>(Modifier::Shift));
    break;
  case Key::CtrlLeft:
  case Key::CtrlRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<unsigned int>(m_impl->currentMods) &
                              ~static_cast<unsigned int>(Modifier::Ctrl));
    break;
  case Key::AltLeft:
  case Key::AltRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<unsigned int>(m_impl->currentMods) &
                              ~static_cast<unsigned int>(Modifier::Alt));
    break;
  case Key::SuperLeft:
  case Key::SuperRight:
    m_impl->currentMods =
        static_cast<Modifier>(static_cast<unsigned int>(m_impl->currentMods) &
                              ~static_cast<unsigned int>(Modifier::Super));
    break;
  default:
    break;
  }
  return result;
}

bool Sender::tap(Key key) {
  if (!keyDown(key)) {
    return false;
  }
  m_impl->delay();
  return keyUp(key);
}

Modifier Sender::activeModifiers() const { return m_impl->currentMods; }

bool Sender::holdModifier(Modifier mod) {
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

bool Sender::releaseModifier(Modifier mod) {
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

bool Sender::releaseAllModifiers() {
  return releaseModifier(Modifier::Shift | Modifier::Ctrl | Modifier::Alt |
                         Modifier::Super);
}

bool Sender::combo(Modifier mods, Key key) {
  if (!holdModifier(mods)) {
    return false;
  }
  m_impl->delay();
  bool tapResult = tap(key);
  m_impl->delay();
  releaseModifier(mods);
  return tapResult;
}

bool Sender::typeText(const std::u32string &text) {
  return m_impl->typeUnicode(text);
}

bool Sender::typeText(const std::string &utf8Text) {
  std::u32string utf32;
  size_t utf8Index = 0;
  static constexpr unsigned char kUtf8Mask6 = 0x3F;
  static constexpr unsigned char kUtf8Mask5 = 0x1F;
  static constexpr unsigned char kUtf8Mask4 = 0x0F;
  static constexpr unsigned char kUtf8Mask3 = 0x07;
  static constexpr unsigned char kUtf8Mask7 = 0x80;
  static constexpr unsigned char kUtf8Mask8 = 0xC0;
  static constexpr unsigned char kUtf8Mask9 = 0xE0;
  static constexpr unsigned char kUtf8Mask10 = 0xF0;
  static constexpr unsigned char kUtf8Mask11 = 0xF8;
  static constexpr unsigned int kUtf8Shift6 = 6;
  static constexpr unsigned int kUtf8Shift12 = 12;
  static constexpr unsigned int kUtf8Shift18 = 18;

  while (utf8Index < utf8Text.size()) {
    char32_t decodedCodepoint = 0;
    auto utf8Char = static_cast<unsigned char>(utf8Text[utf8Index]);
    if ((utf8Char & kUtf8Mask7) == 0) {
      decodedCodepoint = utf8Char;
      utf8Index += 1;
    } else if ((utf8Char & kUtf8Mask9) == kUtf8Mask8) {
      decodedCodepoint = (utf8Char & kUtf8Mask5) << kUtf8Shift6;
      if (utf8Index + 1 < utf8Text.size()) {
        decodedCodepoint |=
            (static_cast<unsigned char>(utf8Text[utf8Index + 1]) & kUtf8Mask6);
      }
      utf8Index += 2;
    } else if ((utf8Char & kUtf8Mask10) == kUtf8Mask9) {
      decodedCodepoint = (utf8Char & kUtf8Mask4) << kUtf8Shift12;
      if (utf8Index + 1 < utf8Text.size()) {
        decodedCodepoint |=
            (static_cast<unsigned char>(utf8Text[utf8Index + 1]) & kUtf8Mask6)
            << kUtf8Shift6;
      }
      if (utf8Index + 2 < utf8Text.size()) {
        decodedCodepoint |=
            (static_cast<unsigned char>(utf8Text[utf8Index + 2]) & kUtf8Mask6);
      }
      utf8Index += 3;
    } else if ((utf8Char & kUtf8Mask11) == kUtf8Mask10) {
      decodedCodepoint = (utf8Char & kUtf8Mask3) << kUtf8Shift18;
      if (utf8Index + 1 < utf8Text.size()) {
        decodedCodepoint |=
            (static_cast<unsigned char>(utf8Text[utf8Index + 1]) & kUtf8Mask6)
            << kUtf8Shift12;
      }
      if (utf8Index + 2 < utf8Text.size()) {
        decodedCodepoint |=
            (static_cast<unsigned char>(utf8Text[utf8Index + 2]) & kUtf8Mask6)
            << kUtf8Shift6;
      }
      if (utf8Index + 3 < utf8Text.size()) {
        decodedCodepoint |=
            (static_cast<unsigned char>(utf8Text[utf8Index + 3]) & kUtf8Mask6);
      }
      utf8Index += 4;
    } else {
      utf8Index += 1;
      continue;
    }
    utf32.push_back(decodedCodepoint);
  }
  return typeText(utf32);
}

bool Sender::typeCharacter(char32_t codepoint) {
  return typeText(std::u32string(1, codepoint));
}

void Sender::flush() {
  // CGEventPost is synchronous
}

void Sender::setKeyDelay(uint32_t delayUs) {
  m_impl->keyDelayUs = delayUs;
}

} // namespace typr::io

#endif // __APPLE__
