#ifdef __APPLE__

#include <typr-io/listener.hpp>

#import <Foundation/Foundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <array>

namespace typr::io {

namespace {

// Convert CG event flags to our Modifier bitset
Modifier flagsToModifier(CGEventFlags flags) {
  Modifier mods = Modifier::None;
  if (flags & kCGEventFlagMaskShift) {
    mods = mods | Modifier::Shift;
  }
  if (flags & kCGEventFlagMaskControl) {
    mods = mods | Modifier::Ctrl;
  }
  if (flags & kCGEventFlagMaskAlternate) {
    mods = mods | Modifier::Alt;
  }
  if (flags & kCGEventFlagMaskCommand) {
    mods = mods | Modifier::Super;
  }
  if (flags & kCGEventFlagMaskAlphaShift) {
    mods = mods | Modifier::CapsLock;
  }
  return mods;
}

static bool output_debug_enabled() {
  static int d = -1;
  if (d != -1)
    return d != 0;
  const char *env = getenv("TYPR_OSK_DEBUG_BACKEND");
  if (!env) {
    // Default to enabled for the time being while testing
    d = 1;
  } else {
    d = (env[0] != '0');
  }
  return d != 0;
}

} // namespace

struct Listener::Impl {
  Impl() : running(false), eventTap(nullptr), runLoopSource(nullptr), runLoop(nullptr) {
    initKeyMap();
  }

  ~Impl() {
    stop();
  }

  bool start(Callback cb) {
    std::lock_guard<std::mutex> lk(cbMutex);
    if (running.load())
      return false;
    callback = std::move(cb);
    running.store(true);
    ready.store(false);
    worker = std::thread([this]() { threadMain(); });

    // Wait briefly for successful startup (event tap installed) or failure.
    for (int i = 0; i < 40; ++i) {
      if (!running.load())
        return false;
      if (ready.load())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready.load();
  }

  void stop() {
    if (!running.load())
      return;
    running.store(false);

    // Stop the CFRunLoop (may be called from another thread)
    if (runLoop != nullptr) {
      CFRunLoopStop(runLoop);
    }

    if (worker.joinable())
      worker.join();

    // Ensure resources cleaned
    if (eventTap) {
      CGEventTapEnable(eventTap, false);
      CFRelease(eventTap);
      eventTap = nullptr;
    }
    if (runLoopSource) {
      CFRelease(runLoopSource);
      runLoopSource = nullptr;
    }
    runLoop = nullptr;
    {
      std::lock_guard<std::mutex> lk(cbMutex);
      callback = nullptr;
    }
  }

  bool isRunning() const { return running.load(); }

private:
  // Thread main installs an event tap and runs a CFRunLoop to receive events.
  void threadMain() {
    // Create an event mask for key down + key up
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);

    // Try to create an event tap on the session level. If this fails, the
    // system likely blocked input monitoring (or another error occurred).
    eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask,
                                &Impl::eventTapCallback, this);
    if (eventTap == nullptr) {
    // Failed to create event tap -> nothing we can do here
    running.store(false);
    ready.store(false);
    if (output_debug_enabled()) {
      fprintf(stderr, "[typr-backend] Listener (macOS): failed to create CGEventTap. Input Monitoring permission may be missing.\n");
    }
    return;
  }

    // Create a runloop source and add it to the current run loop
    runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);
    // Enable the tap
    CGEventTapEnable(eventTap, true);

    // Signal successful initialization and optionally log
    ready.store(true);
    if (output_debug_enabled()) {
      fprintf(stderr, "[typr-backend] Listener (macOS): event tap created and enabled\n");
    }

    // Store the run loop so `stop` can stop it from another thread
    runLoop = CFRunLoopGetCurrent();

    // Run the loop until stop() calls CFRunLoopStop()
    CFRunLoopRun();

    // Clean up (some cleanup is also done in stop())
    if (eventTap) {
      CGEventTapEnable(eventTap, false);
      CFRelease(eventTap);
      eventTap = nullptr;
    }
    if (runLoopSource) {
      CFRelease(runLoopSource);
      runLoopSource = nullptr;
    }
    runLoop = nullptr;
  }

  // Event tap callback (invoked on the run loop thread)
  static CGEventRef eventTapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *userInfo) {
    Impl *self = reinterpret_cast<Impl *>(userInfo);
    if (!self)
      return event;

    // Re-enable tap if it was disabled by a timeout
    if (type == kCGEventTapDisabledByTimeout) {
      if (self->eventTap)
        CGEventTapEnable(self->eventTap, true);
      return event;
    }

    if (type != kCGEventKeyDown && type != kCGEventKeyUp)
      return event;

    bool pressed = (type == kCGEventKeyDown);
    CGKeyCode keyCode = static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));

    // Unicode extraction - macOS gives us UTF-16 UniChar sequences
    std::array<UniChar, 4> uniBuf{};
    UniCharCount actualLen = 0;
    CGEventKeyboardGetUnicodeString(event, static_cast<UniCharCount>(uniBuf.size()), &actualLen, uniBuf.data());

    char32_t codepoint = 0;
    if (actualLen > 0) {
      if (actualLen == 1) {
        codepoint = static_cast<char32_t>(uniBuf[0]);
      } else if (actualLen == 2) {
        // Combine surrogate pair
        uint32_t high = static_cast<uint32_t>(uniBuf[0]);
        uint32_t low = static_cast<uint32_t>(uniBuf[1]);
        if ((0xD800 <= high && high <= 0xDBFF) && (0xDC00 <= low && low <= 0xDFFF)) {
          codepoint = 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
        } else {
          codepoint = 0; // invalid surrogate pair; drop
        }
      } else {
        // More complex output - take first char for simplicity
        codepoint = static_cast<char32_t>(uniBuf[0]);
      }
    }

    // Map CGKeyCode to our Key enum
    Key mapped = Key::Unknown;
    auto it = self->cgKeyToKey.find(keyCode);
    if (it != self->cgKeyToKey.end()) {
      mapped = it->second;
    }

    // Modifiers
    CGEventFlags flags = CGEventGetFlags(event);
    Modifier mods = flagsToModifier(flags);

    // Invoke user callback outside the lock
    Callback cbCopy;
    {
      std::lock_guard<std::mutex> lk(self->cbMutex);
      cbCopy = self->callback;
    }
    if (cbCopy) {
      cbCopy(static_cast<char32_t>(codepoint), mapped, mods, pressed);
    }

    if (output_debug_enabled()) {
      std::string kname = "Unknown";
      auto kIt = self->cgKeyToKey.find(keyCode);
      if (kIt != self->cgKeyToKey.end()) kname = keyToString(kIt->second);
      fprintf(stderr, "[typr-backend] Listener (macOS) %s: keycode=%u key=%s cp=%u mods=%u\n",
              pressed ? "press" : "release", (unsigned)keyCode, kname.c_str(), (unsigned)codepoint, (unsigned)mods);
    }

    // Let the event pass through unchanged
    return event;
  }

  // Build a reverse mapping CGKeyCode -> Key using the same discovery logic
  // used by the InputBackend on macOS.
  void initKeyMap() {
    cgKeyToKey.clear();

    TISInputSourceRef currentKeyboard = TISCopyCurrentKeyboardInputSource();
    if (currentKeyboard == nullptr) {
      // Fall back to using constants only
      fillFallbacks();
      return;
    }

    const auto *layoutData = static_cast<CFDataRef>(TISGetInputSourceProperty(
        currentKeyboard, kTISPropertyUnicodeKeyLayoutData));
    if (layoutData == nullptr) {
      CFRelease(currentKeyboard);
      fillFallbacks();
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
        } else if (firstUnicodeChar == kAsciiCR || firstUnicodeChar == kAsciiLF) {
          mappedKeyString = "enter";
        } else if (firstUnicodeChar < kAsciiMax) {
          mappedKeyString = std::string(1, static_cast<char>(firstUnicodeChar));
        } else {
          // Non-ASCII mapping isn't covered by `Key` enum; skip
          continue;
        }

        Key mappedKeyEnum = stringToKey(mappedKeyString);
        if (mappedKeyEnum != Key::Unknown) {
          if (cgKeyToKey.find(static_cast<CGKeyCode>(keyCode)) == cgKeyToKey.end()) {
            cgKeyToKey[static_cast<CGKeyCode>(keyCode)] = mappedKeyEnum;
          }
        }
      }
    }

    CFRelease(currentKeyboard);

    // Add canonical fallbacks for keys that may not be covered by layout scan
    fillFallbacks();
  }

  // Fallback explicit mappings for non-printable keys / modifiers
  void fillFallbacks() {
    auto setIfMissing = [this](Key keyToSet, CGKeyCode code) {
      if (this->cgKeyToKey.find(code) == this->cgKeyToKey.end()) {
        this->cgKeyToKey[code] = keyToSet;
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

    // ANSI punctuation
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
  }

  // Safely invoke user callback
  void invokeCallback(char32_t cp, Key k, Modifier mods, bool pressed) {
    Callback cbCopy;
    {
      std::lock_guard<std::mutex> lk(cbMutex);
      cbCopy = callback;
    }
    if (cbCopy) {
      cbCopy(cp, k, mods, pressed);
    }
  }

  std::thread worker;
  std::atomic_bool running;
  std::atomic<bool> ready{false};
  Callback callback;
  std::mutex cbMutex;

  // CF / CG resources on the run loop thread
  CFMachPortRef eventTap;
  CFRunLoopSourceRef runLoopSource;
  CFRunLoopRef runLoop;

  // Reverse mapping
  std::unordered_map<CGKeyCode, Key> cgKeyToKey;
};

// OutputListener public wrappers
Listener::Listener() : m_impl(std::make_unique<Impl>()) {}
Listener::~Listener() { stop(); }
Listener::Listener(Listener &&) noexcept = default;
Listener &Listener::operator=(Listener &&) noexcept = default;
bool Listener::start(Callback cb) {
  return m_impl ? m_impl->start(std::move(cb)) : false;
}
void Listener::stop() {
  if (m_impl)
    m_impl->stop();
}
bool Listener::isListening() const {
  return m_impl ? m_impl->isRunning() : false;
}

} // namespace backend

#endif // __APPLE__
