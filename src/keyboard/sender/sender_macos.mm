/**
 * @file keyboard/sender/sender_macos.mm
 * @brief macOS implementation of axidev::io::keyboard::Sender.
 *
 * Uses Core Graphics (Quartz) event services for keyboard injection,
 * including both physical key events and Unicode text injection.
 */

#ifdef __APPLE__

#include <axidev-io/keyboard/sender.hpp>

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#include <chrono>
#include <thread>
#include <axidev-io/log.hpp>
#include <unordered_map>

#include "keyboard/common/macos_keymap.hpp"

namespace axidev::io::keyboard {

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
  // Map from character to keycode and modifiers
  std::unordered_map<char32_t, KeyMapping> charToKeycode;

  Impl()
      : eventSource(CGEventSourceCreate(kCGEventSourceStateHIDSystemState)),
        ready(AXIsProcessTrustedWithOptions(nullptr) != 0U) {
    initKeyMap();
    AXIDEV_IO_LOG_INFO("Sender (macOS): Impl created; ready=%u",
                     static_cast<unsigned>(ready));
    AXIDEV_IO_LOG_DEBUG("Sender (macOS): eventSource=%p", eventSource);
    AXIDEV_IO_LOG_DEBUG("Sender (macOS): keyMap initialized with %zu entries",
                      keyMap.size());
    AXIDEV_IO_LOG_DEBUG("Sender (macOS): charToKeycode initialized with %zu entries",
                      charToKeycode.size());
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
        keyMap(std::move(other.keyMap)),
        charToKeycode(std::move(other.charToKeycode)) {
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
    charToKeycode = std::move(other.charToKeycode);

    other.eventSource = nullptr;
    other.currentMods = Modifier::None;
    other.keyDelayUs = 0;
    other.ready = false;

    return *this;
  }

  void initKeyMap() {
    auto km = ::axidev::io::keyboard::detail::initMacOSKeyMap();
    keyMap = std::move(km.keyToCode);
    charToKeycode = std::move(km.charToKeycode);
  }

  [[nodiscard]] CGKeyCode macKeyCodeFor(Key key) const {
    auto keyMapIt = keyMap.find(key);
    static constexpr CGKeyCode kInvalidKeyCode = UINT16_MAX;
    if (keyMapIt != keyMap.end())
      return keyMapIt->second;
    AXIDEV_IO_LOG_DEBUG("Sender (macOS): macKeyCodeFor(key=%s) -> invalid",
                      keyToString(key).c_str());
    return kInvalidKeyCode;
  }

  bool sendKey(Key key, bool down) const {
    CGKeyCode keyCode = macKeyCodeFor(key);
    static constexpr CGKeyCode kInvalidKeyCode = UINT16_MAX;
    if (keyCode == kInvalidKeyCode) {
      AXIDEV_IO_LOG_DEBUG("Sender (macOS): sendKey - no mapping for key=%s",
                        keyToString(key).c_str());
      return false;
    }

    CGEventRef event = CGEventCreateKeyboardEvent(eventSource, keyCode, down);
    if (event == nullptr) {
      AXIDEV_IO_LOG_ERROR(
          "Sender (macOS): CGEventCreateKeyboardEvent returned null for key=%s",
          keyToString(key).c_str());
      return false;
    }

    // Apply current modifier state
    CGEventSetFlags(event, modifierToFlags(currentMods));
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    AXIDEV_IO_LOG_DEBUG("Sender (macOS): sendKey key=%s keycode=%u down=%u",
                      keyToString(key).c_str(), static_cast<unsigned>(keyCode),
                      static_cast<unsigned>(down));
    return true;
  }

  [[nodiscard]] bool typeUnicode(const std::u32string &text) const {
    AXIDEV_IO_LOG_DEBUG("Sender (macOS): typeUnicode called len=%zu",
                      text.size());
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
        AXIDEV_IO_LOG_ERROR(
            "Sender (macOS): typeUnicode failed to create CGEvents for chunk");
        return false;
      }

      CGEventKeyboardSetUnicodeString(eventDown, chunkLength,
                                      &utf16[utf16Index]);
      CGEventKeyboardSetUnicodeString(eventUp, chunkLength, &utf16[utf16Index]);

      CGEventPost(kCGHIDEventTap, eventDown);
      CGEventPost(kCGHIDEventTap, eventUp);
      AXIDEV_IO_LOG_DEBUG("Sender (macOS): posted unicode chunk length=%zu",
                        chunkLength);

      CFRelease(eventDown);
      CFRelease(eventUp);
    }
    AXIDEV_IO_LOG_DEBUG("Sender (macOS): typeUnicode completed");
    return true;
  }

  void delay() const {
    if (keyDelayUs > 0) {
      AXIDEV_IO_LOG_DEBUG("Sender (macOS): delay %u us", keyDelayUs);
      std::this_thread::sleep_for(std::chrono::microseconds(keyDelayUs));
    }
  }
};

Sender::Sender() : m_impl(std::make_unique<Impl>()) {
  AXIDEV_IO_LOG_INFO("Sender (macOS): constructed, ready=%u",
                   static_cast<unsigned>(isReady()));
}
Sender::~Sender() = default;
Sender::Sender(Sender &&) noexcept = default;
Sender &Sender::operator=(Sender &&) noexcept = default;

BackendType Sender::type() const {
  AXIDEV_IO_LOG_DEBUG("Sender::type() -> MacOS");
  return BackendType::MacOS;
}

Capabilities Sender::capabilities() const {
  AXIDEV_IO_LOG_DEBUG("Sender::capabilities() called (macOS)");
  return {
      .canInjectKeys = m_impl->ready,
      .canInjectText = m_impl->ready,
      .canSimulateHID = false, // macOS CGEvent is not true HID
      .supportsKeyRepeat = false,
      .needsAccessibilityPerm = true,
      .needsInputMonitoringPerm = false,
      .needsUinputAccess = false,
  };
}

bool Sender::isReady() const {
  bool r = m_impl ? m_impl->ready : false;
  AXIDEV_IO_LOG_DEBUG("Sender::isReady() -> %u", static_cast<unsigned>(r));
  return r;
}

bool Sender::requestPermissions() {
  AXIDEV_IO_LOG_DEBUG("Sender::requestPermissions() called (macOS)");
  NSDictionary *opts =
      @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
  m_impl->ready =
      (AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts) != 0U);
  AXIDEV_IO_LOG_INFO("Sender (macOS): requestPermissions result=%u",
                   static_cast<unsigned>(m_impl->ready));
  return m_impl->ready;
}

// Internal helper to send a raw key event (used by public API)
bool Sender::sendRawKey(Key key, bool down) {
  if (down) {
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
  } else {
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
  }
  return m_impl->sendKey(key, down);
}

bool Sender::keyDown(KeyWithModifier keyMod) {
  AXIDEV_IO_LOG_DEBUG(
      "Sender::keyDown(%s)",
      keyToStringWithModifier(keyMod.key, keyMod.requiredMods).c_str());
  // Press required modifiers first
  if (!holdModifier(keyMod.requiredMods)) {
    return false;
  }
  bool ok = sendRawKey(keyMod.key, true);
  AXIDEV_IO_LOG_DEBUG("Sender::keyDown result=%u", static_cast<unsigned>(ok));
  return ok;
}

bool Sender::keyUp(KeyWithModifier keyMod) {
  AXIDEV_IO_LOG_DEBUG(
      "Sender::keyUp(%s)",
      keyToStringWithModifier(keyMod.key, keyMod.requiredMods).c_str());
  bool result = sendRawKey(keyMod.key, false);
  // Release modifiers that were specified
  releaseModifier(keyMod.requiredMods);
  AXIDEV_IO_LOG_DEBUG("Sender::keyUp result=%u", static_cast<unsigned>(result));
  return result;
}

bool Sender::tap(KeyWithModifier keyMod) {
  AXIDEV_IO_LOG_DEBUG(
      "Sender::tap(%s)",
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
  AXIDEV_IO_LOG_DEBUG("Sender::tap result=%u", static_cast<unsigned>(ok));
  return ok;
}

Modifier Sender::activeModifiers() const {
  Modifier mods = m_impl->currentMods;
  AXIDEV_IO_LOG_DEBUG("Sender::activeModifiers() -> %u",
                    static_cast<unsigned>(mods));
  return mods;
}

bool Sender::holdModifier(Modifier mod) {
  AXIDEV_IO_LOG_DEBUG("Sender::holdModifier(mod=%u)", static_cast<unsigned>(mod));
  bool allModifiersPressed = true;
  if (hasModifier(mod, Modifier::Shift))
    allModifiersPressed &= sendRawKey(Key::ShiftLeft, true);
  if (hasModifier(mod, Modifier::Ctrl))
    allModifiersPressed &= sendRawKey(Key::CtrlLeft, true);
  if (hasModifier(mod, Modifier::Alt))
    allModifiersPressed &= sendRawKey(Key::AltLeft, true);
  if (hasModifier(mod, Modifier::Super))
    allModifiersPressed &= sendRawKey(Key::SuperLeft, true);
  AXIDEV_IO_LOG_DEBUG(
      "Sender::holdModifier result=%u currentMods=%u",
      static_cast<unsigned>(allModifiersPressed),
      static_cast<unsigned>(m_impl ? m_impl->currentMods : Modifier::None));
  return allModifiersPressed;
}

bool Sender::releaseModifier(Modifier mod) {
  AXIDEV_IO_LOG_DEBUG("Sender::releaseModifier(mod=%u)",
                    static_cast<unsigned>(mod));
  bool allModifiersReleased = true;
  if (hasModifier(mod, Modifier::Shift))
    allModifiersReleased &= sendRawKey(Key::ShiftLeft, false);
  if (hasModifier(mod, Modifier::Ctrl))
    allModifiersReleased &= sendRawKey(Key::CtrlLeft, false);
  if (hasModifier(mod, Modifier::Alt))
    allModifiersReleased &= sendRawKey(Key::AltLeft, false);
  if (hasModifier(mod, Modifier::Super))
    allModifiersReleased &= sendRawKey(Key::SuperLeft, false);
  AXIDEV_IO_LOG_DEBUG(
      "Sender::releaseModifier result=%u currentMods=%u",
      static_cast<unsigned>(allModifiersReleased),
      static_cast<unsigned>(m_impl ? m_impl->currentMods : Modifier::None));
  return allModifiersReleased;
}

bool Sender::releaseAllModifiers() {
  AXIDEV_IO_LOG_DEBUG("Sender::releaseAllModifiers()");
  bool ok = releaseModifier(Modifier::Shift | Modifier::Ctrl | Modifier::Alt |
                            Modifier::Super);
  AXIDEV_IO_LOG_DEBUG("Sender::releaseAllModifiers result=%u",
                    static_cast<unsigned>(ok));
  return ok;
}

bool Sender::typeText(const std::u32string &text) {
  AXIDEV_IO_LOG_DEBUG("Sender::typeText (utf32) called with %zu codepoints",
                    text.size());
  return m_impl->typeUnicode(text);
}

bool Sender::typeText(const std::string &utf8Text) {
  AXIDEV_IO_LOG_DEBUG("Sender::typeText (utf8) called len=%zu", utf8Text.size());
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
        decodedCodepoint |= (utf8Text[utf8Index + 1] & 0x3F);
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
  AXIDEV_IO_LOG_DEBUG("Sender::typeCharacter(codepoint=%u)",
                    static_cast<unsigned>(codepoint));
  return typeText(std::u32string(1, codepoint));
}

void Sender::flush() {
  AXIDEV_IO_LOG_DEBUG("Sender::flush()");
  // CGEventPost is synchronous
}

void Sender::setKeyDelay(uint32_t delayUs) {
  AXIDEV_IO_LOG_DEBUG("Sender::setKeyDelay(%u)", delayUs);
  m_impl->keyDelayUs = delayUs;
}

} // namespace axidev::io::keyboard

#endif // __APPLE__
