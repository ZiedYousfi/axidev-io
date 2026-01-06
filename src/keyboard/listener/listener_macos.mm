/**
 * @file keyboard/listener/listener_macos.mm
 * @brief macOS implementation of axidev::io::keyboard::Listener.
 *
 * Uses Core Graphics event taps to monitor global keyboard events.
 * Requires Input Monitoring permission on macOS 10.15+.
 */

#ifdef __APPLE__

#include <axidev-io/keyboard/listener.hpp>

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <axidev-io/log.hpp>
#include <unordered_map>

#include "keyboard/common/macos_keymap.hpp"

namespace axidev::io::keyboard {

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

static bool output_debug_enabled() { return ::axidev::io::log::debugEnabled(); }

/**
 * @brief Derive the canonical lowercase codepoint for a Key enum value.
 *
 * For letter keys (A-Z), returns the lowercase ASCII codepoint ('a'-'z').
 * For number keys (Num0-Num9), returns the ASCII digit ('0'-'9').
 * For other keys, returns 0 (caller should use the system-provided codepoint).
 *
 * This ensures consistent character output regardless of keyboard layout
 * mismatches between the keymap initialization and event delivery.
 */
static char32_t codepointFromKey(Key key) {
  // Letters A-Z -> 'a'-'z' (lowercase)
  if (key >= Key::A && key <= Key::Z) {
    return static_cast<char32_t>(
        'a' + (static_cast<int>(key) - static_cast<int>(Key::A)));
  }
  // Numbers 0-9
  if (key >= Key::Num0 && key <= Key::Num9) {
    return static_cast<char32_t>(
        '0' + (static_cast<int>(key) - static_cast<int>(Key::Num0)));
  }
  // Numpad 0-9
  if (key >= Key::Numpad0 && key <= Key::Numpad9) {
    return static_cast<char32_t>(
        '0' + (static_cast<int>(key) - static_cast<int>(Key::Numpad0)));
  }
  return 0;
}

} // namespace

struct Listener::Impl {
  Impl()
      : running(false), eventTap(nullptr), runLoopSource(nullptr),
        runLoop(nullptr) {
    initKeyMap();
  }

  ~Impl() { stop(); }

  bool start(Callback cb) {
    std::lock_guard<std::mutex> lk(cbMutex);
    if (running.load())
      return false;
    AXIDEV_IO_LOG_INFO("Listener (macOS): start requested");
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
    bool ok = ready.load();
    AXIDEV_IO_LOG_DEBUG("Listener (macOS): start result=%u",
                      static_cast<unsigned>(ok));
    return ok;
  }

  void stop() {
    if (!running.load())
      return;
    AXIDEV_IO_LOG_INFO("Listener (macOS): stop requested");
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
    AXIDEV_IO_LOG_INFO("Listener (macOS): stopped");
  }

  bool isRunning() const { return running.load(); }

private:
  // Thread main installs an event tap and runs a CFRunLoop to receive events.
  void threadMain() {
    // Create an event mask for key down + key up
    CGEventMask mask =
        CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);

    // Try to create an event tap on the session level. If this fails, the
    // system likely blocked input monitoring (or another error occurred).
    eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionListenOnly, mask,
                                &Impl::eventTapCallback, this);
    if (eventTap == nullptr) {
      // Failed to create event tap -> nothing we can do here
      running.store(false);
      ready.store(false);
      AXIDEV_IO_LOG_ERROR("Listener (macOS): failed to create CGEventTap. Input "
                        "Monitoring permission may be missing.");
      return;
    }

    // Create a runloop source and add it to the current run loop
    runLoopSource =
        CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource,
                       kCFRunLoopCommonModes);
    // Enable the tap
    CGEventTapEnable(eventTap, true);

    // Signal successful initialization and optionally log
    ready.store(true);
    AXIDEV_IO_LOG_INFO("Listener (macOS): event tap created and enabled");

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
  static CGEventRef eventTapCallback(CGEventTapProxy proxy, CGEventType type,
                                     CGEventRef event, void *userInfo) {
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
    CGKeyCode keyCode = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));

    // Unicode extraction - macOS gives us UTF-16 UniChar sequences
    std::array<UniChar, 4> uniBuf{};
    UniCharCount actualLen = 0;
    CGEventKeyboardGetUnicodeString(event,
                                    static_cast<UniCharCount>(uniBuf.size()),
                                    &actualLen, uniBuf.data());

    char32_t codepoint = 0;
    if (actualLen > 0) {
      if (actualLen == 1) {
        codepoint = static_cast<char32_t>(uniBuf[0]);
      } else if (actualLen == 2) {
        // Combine surrogate pair
        uint32_t high = static_cast<uint32_t>(uniBuf[0]);
        uint32_t low = static_cast<uint32_t>(uniBuf[1]);
        if ((0xD800 <= high && high <= 0xDBFF) &&
            (0xDC00 <= low && low <= 0xDFFF)) {
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
    auto it = self->keyMap.codeToKey.find(keyCode);
    if (it != self->keyMap.codeToKey.end()) {
      mapped = it->second;
    }

    // Modifiers
    CGEventFlags flags = CGEventGetFlags(event);
    Modifier mods = flagsToModifier(flags);

    // Try modifier-aware key resolution to get the correct Key for shifted
    // characters. For example, Shift+1 on US keyboard produces '!' which maps
    // to Key::Exclamation.
    Key modifierAwareKey =
        ::axidev::io::keyboard::detail::resolveKeyFromCodeAndMods(
            self->keyMap, keyCode, mods);
    if (modifierAwareKey != Key::Unknown) {
      // Use the modifier-aware key, but keep the base key in 'mapped' for
      // control/navigation keys where we want the physical key identity.
      // For printable characters, prefer the modifier-aware resolution.
      if (modifierAwareKey != mapped) {
        AXIDEV_IO_LOG_DEBUG(
            "Listener (macOS): modifier-aware resolution: keycode=%u "
            "mods=0x%02X -> Key::%s (base was Key::%s)",
            static_cast<unsigned>(keyCode), static_cast<unsigned>(mods),
            keyToString(modifierAwareKey).c_str(), keyToString(mapped).c_str());
        mapped = modifierAwareKey;
      }
    }

    // For letter and number keys, derive the codepoint from the Key enum
    // rather than trusting CGEventKeyboardGetUnicodeString. This ensures
    // consistent output regardless of keyboard layout mismatches between
    // keymap initialization and event delivery (e.g., AZERTY vs QWERTY).
    // Only override when Shift is not pressed (to get lowercase letters).
    if (mapped != Key::Unknown && !hasModifier(mods, Modifier::Shift)) {
      char32_t derivedCp = codepointFromKey(mapped);
      if (derivedCp != 0) {
        codepoint = derivedCp;
      }
    }

    // Treat Enter and Backspace as control keys (non-printable). If we pass a
    // non-zero codepoint for these keys the test callback will append that
    // control character into the observed string rather than handling the
    // key event; clear the codepoint so consumers observe the key event
    // (e.g., key == Key::Enter / Key::Backspace) and can react accordingly.
    if (mapped == Key::Enter || mapped == Key::Backspace) {
      codepoint = 0;
    }

    // Handle a couple of platform quirks observed on macOS:
    //  1) Some release events are delivered multiple times for the same key.
    //     Debounce rapid duplicate releases to avoid emitting duplicate
    //     characters to the consumer.
    //  2) In some cases the release event does not contain a Unicode string
    //     (actualLen == 0) while the corresponding press did. Cache the last
    //     press codepoint and use it as a fallback for the release.
    static constexpr std::chrono::milliseconds kReleaseDebounceMs{50};

    if (pressed) {
      // On press, remember the unicode output (if any) for potential use on
      // the paired release event.
      if (codepoint != 0) {
        self->lastPressCp[keyCode] = codepoint;
      } else {
        // Clear any stale entry for this keycode when press does not produce
        // a unicode character (e.g., modifiers).
        self->lastPressCp.erase(keyCode);
      }
    } else {
      // On release, debounce rapid duplicates coming from the system.
      auto now = std::chrono::steady_clock::now();
      auto rtIt = self->lastReleaseTime.find(keyCode);
      auto sigIt = self->lastReleaseSig.find(keyCode);
      if (rtIt != self->lastReleaseTime.end() &&
          sigIt != self->lastReleaseSig.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - rtIt->second);
        if (elapsed < kReleaseDebounceMs && sigIt->second.first == codepoint &&
            sigIt->second.second == mods) {
          if (output_debug_enabled()) {
            std::string kname = keyToString(mapped);
            AXIDEV_IO_LOG_DEBUG(
                "Listener (macOS): ignoring duplicate release (same cp+mods) "
                "for keycode=%u key=%s cp=%u mods=%u",
                (unsigned)keyCode, kname.c_str(), (unsigned)codepoint,
                (unsigned)mods);
          }
          // Update timestamp & signature so subsequent quick duplicates remain
          // debounced.
          self->lastReleaseTime[keyCode] = now;
          self->lastReleaseSig[keyCode] = std::make_pair(codepoint, mods);
          return event;
        }
      }
      // Not a duplicate matching the last cp+mods -> record the new
      // signature/time.
      self->lastReleaseTime[keyCode] = now;
      self->lastReleaseSig[keyCode] = std::make_pair(codepoint, mods);

      // If the release lacks a unicode string, fall back to the cached press
      // codepoint for this keycode (if available).
      if (codepoint == 0) {
        auto cpIt = self->lastPressCp.find(keyCode);
        if (cpIt != self->lastPressCp.end()) {
          codepoint = cpIt->second;
          if (output_debug_enabled()) {
            AXIDEV_IO_LOG_DEBUG("Listener (macOS): using last-press cp=%u for "
                              "release keycode=%u",
                              static_cast<unsigned>(codepoint),
                              static_cast<unsigned>(keyCode));
          }
        }
      }

      // We've handled the release, clear the cached press codepoint so we
      // don't accidentally reuse it for future, unrelated events.
      self->lastPressCp.erase(keyCode);
    }

    // Invoke user callback outside the lock
    Callback cbCopy;
    {
      std::lock_guard<std::mutex> lk(self->cbMutex);
      cbCopy = self->callback;
    }
    if (cbCopy) {
      cbCopy(static_cast<char32_t>(codepoint), KeyWithModifier(mapped, mods),
             pressed);
    }

    {
      std::string kname = keyToString(mapped);
      AXIDEV_IO_LOG_DEBUG("Listener (macOS) %s: keycode=%u key=%s cp=%u mods=%u",
                        pressed ? "press" : "release", (unsigned)keyCode,
                        kname.c_str(), (unsigned)codepoint, (unsigned)mods);
    }

    // Let the event pass through unchanged
    return event;
  }

  // Build key map using the same discovery logic used by the InputBackend on
  // macOS. Store the full key map so we can use modifier-aware key resolution.
  void initKeyMap() {
    keyMap = ::axidev::io::keyboard::detail::initMacOSKeyMap();
  }

  // Safely invoke user callback
  void invokeCallback(char32_t cp, KeyWithModifier keyMod, bool pressed) {
    Callback cbCopy;
    {
      std::lock_guard<std::mutex> lk(cbMutex);
      cbCopy = callback;
    }
    if (cbCopy) {
      cbCopy(cp, keyMod, pressed);
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

  // Full key map for modifier-aware key resolution
  ::axidev::io::keyboard::detail::MacOSKeyMap keyMap;

  // Last-seen unicode codepoint for keycodes (press -> release fallback).
  // Some macOS configurations produce keyup events without a Unicode string
  // while the corresponding keydown contained the character. We cache the
  // last press codepoint per keycode so releases can fall back to it.
  std::unordered_map<CGKeyCode, char32_t> lastPressCp;

  // Timestamp of the last release seen for a given keycode. Used to debounce
  // duplicate release events which can otherwise cause repeated characters in
  // the observed output.
  std::unordered_map<CGKeyCode, std::chrono::steady_clock::time_point>
      lastReleaseTime;
  std::unordered_map<CGKeyCode, std::pair<char32_t, Modifier>> lastReleaseSig;
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

} // namespace axidev::io::keyboard

#endif // __APPLE__
