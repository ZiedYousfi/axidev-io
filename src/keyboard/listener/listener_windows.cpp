/**
 * @file keyboard/listener/listener_windows.cpp
 * @brief Windows implementation of typr::io::keyboard::Listener.
 *
 * Uses a low-level keyboard hook (WH_KEYBOARD_LL) to observe global keyboard
 * activity, translate virtual keys and produced characters into logical
 * `Key` values and Unicode codepoints where possible, and forward events to
 * the public `Listener` callback. Callback invocations occur on the hook
 * thread and therefore must be thread-safe and avoid long/blocking work.
 *
 * @note This implementation focuses on common printable characters and physical
 *       key mappings and intentionally avoids complex IME/dead-key composition
 *       behavior to remain lightweight and predictable.
 */

#ifdef _WIN32

#include <typr-io/keyboard/listener.hpp>

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <typr-io/log.hpp>
#include <unordered_map>
#include <vector>

namespace typr::io::keyboard {

namespace {

/**
 * @internal
 * @brief Test whether a virtual-key code should be treated as an \"extended\"
 * key.
 *
 * Certain virtual-key codes are treated as extended keys on Windows (for
 * example, some navigation and keypad keys). This helper centralizes that
 * decision so both the Listener and other platform code can remain consistent.
 *
 * @param vk Virtual-key code to test.
 * @return true if the key is considered an extended key and may require
 *         special handling; false otherwise.
 */
bool isExtendedKeyForVK(WORD vk) {
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

/**
 * @internal
 * @brief Check whether debug output is enabled for the listener backend.
 *
 * The behaviour follows the logging facility: it is influenced by the
 * environment and the global log level (legacy `TYPR_OSK_DEBUG_BACKEND` or the
 * newer `TYPR_IO_LOG_LEVEL` mechanism).
 *
 * @return true if debug-level logging is enabled for the process.
 */
static bool output_debug_enabled() { return ::typr::io::log::debugEnabled(); }

} // namespace

/**
 * @internal
 * @brief Pimpl for `typr::io::keyboard::Listener` (Windows / hook-based
 * backend).
 *
 * This structure manages the platform-specific pieces required for the
 * Windows listener: hook installation, the hook worker thread, VK -> Key
 * discovery and state used to translate low-level events into logical keys
 * and Unicode codepoints that are forwarded to the caller's callback.
 *
 * Instances are owned by the public `Listener` facade and are not intended
 * to be manipulated directly by consumers.
 */
struct Listener::Impl {
  Impl() { initKeyMap(); }
  ~Impl() { stop(); }

  /**
   * @internal
   * @brief Start the Windows listener.
   *
   * Installs the low-level keyboard hook on a dedicated thread and stores the
   * provided callback. This function waits briefly for the hook to be
   * installed and returns whether the implementation reported readiness.
   *
   * @param cb Callback invoked for each observed key event. The callback may
   *           be called from the hook thread and therefore must be thread-safe.
   * @return true if the listener started successfully and the hook became
   * ready.
   */
  bool start(Callback cb) {
    if (running.load())
      return false;
    {
      std::lock_guard<std::mutex> lk(cbMutex);
      callback = std::move(cb);
    }
    running.store(true);
    // Mark not-ready until the hook is actually installed.
    ready.store(false);
    TYPR_IO_LOG_INFO("Listener (Windows): start requested");
    worker = std::thread(&Impl::threadMain, this);

    // Wait briefly for the hook to be installed (or to fail). This allows
    // startListening to return an accurate success/failure result.
    for (int i = 0; i < 40; ++i) {
      if (!running.load())
        return false;
      if (ready.load())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool ok = ready.load();
    TYPR_IO_LOG_DEBUG("Listener (Windows): start result=%u",
                      static_cast<unsigned>(ok));
    return ok;
  }

  /**
   * @internal
   * @brief Stop the listener and join the hook thread.
   *
   * Posts a WM_QUIT message to the hook thread to ensure it wakes and exits,
   * then joins the worker thread. Safe to call from any thread.
   */
  void stop() {
    if (!running.load())
      return;
    TYPR_IO_LOG_INFO("Listener (Windows): stop requested");
    running.store(false);

    // Wake the thread by posting WM_QUIT
    DWORD tid = threadId.load();
    if (tid != 0) {
      PostThreadMessage(tid, WM_QUIT, 0, 0);
    }

    if (worker.joinable())
      worker.join();
    TYPR_IO_LOG_INFO("Listener (Windows): stopped");
  }

  /**
   * @internal
   * @brief Query whether the listener's worker thread is currently active.
   * @return true if the implementation is running.
   */
  bool isRunning() const { return running.load(); }

  /**
   * @internal
   * @brief Discover and initialize the mapping from virtual-key (VK) codes
   * to logical `Key` values.
   *
   * This procedure queries the active keyboard layout to map printable
   * characters to logical keys, and inserts sensible fallbacks for common
   * control, navigation and modifier keys so the listener can provide a
   * consistent mapping across Windows systems.
   */
  void initKeyMap() {
    vkToKey.clear();

    HKL layout = GetKeyboardLayout(0);
    BYTE keyState[256]{}; // assume all keys up
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
          continue; // non-ASCII/complex; skip for now
        }
        Key mapped = stringToKey(mappedKeyString);
        if (mapped != Key::Unknown) {
          if (vkToKey.find(static_cast<WORD>(vk)) == vkToKey.end()) {
            vkToKey[static_cast<WORD>(vk)] = mapped;
          }
        }
      }
    }

    // Fallback explicit mappings for common non-printable keys / modifiers
    auto setIfMissing = [this](Key k, WORD v) {
      if (this->vkToKey.find(v) == this->vkToKey.end())
        this->vkToKey[v] = k;
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
  }

private:
  // Thread/Hook plumbing
  std::thread worker;
  std::atomic<bool> running{false};
  std::atomic<DWORD> threadId{0};
  HHOOK hook{nullptr};

  // User callback
  Callback callback;
  std::mutex cbMutex;

  // Hook readiness handshake - set to true once the hook is successfully
  // installed and the listener is active.
  std::atomic<bool> ready{false};

  // Reverse map VK -> Key
  std::unordered_map<WORD, Key> vkToKey;

  // Debounce & release handling (works on the hook thread only).
  // - Record the last codepoint seen on press to use as a fallback on release
  //   when the release event lacks a unicode output.
  // - Debounce rapid duplicate releases (same vk+cp+mods within a short
  //   interval) to avoid emitting duplicate characters to consumers.
  std::unordered_map<WORD, char32_t> lastPressCp;
  std::unordered_map<WORD, std::chrono::steady_clock::time_point>
      lastReleaseTime;
  std::unordered_map<WORD, std::pair<char32_t, Modifier>> lastReleaseSig;

  // The hook proc needs to locate the current instance; allow a single
  // active instance via an atomic pointer. (Simple and practical for the app.)
  static std::atomic<Impl *> s_instance;

  /**
   * @internal
   * @brief Low-level keyboard hook procedure (WH_KEYBOARD_LL).
   *
   * Entry point called by Windows for low-level keyboard events.
   * Translates the parameters into a KBDLLHOOKSTRUCT and forwards the
   * event to the single active Listener instance if present. Returns the
   * result of CallNextHookEx when not handling the event.
   *
   * @param nCode Hook code.
   * @param wParam Message (WM_KEYDOWN/WM_KEYUP/WM_SYSKEYDOWN/WM_SYSKEYUP).
   * @param lParam Pointer to KBDLLHOOKSTRUCT describing the event.
   * @return LRESULT Hook procedure result.
   */
  static LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam,
                                               LPARAM lParam) {
    if (nCode < 0)
      return CallNextHookEx(nullptr, nCode, wParam, lParam);
    auto *kbd = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    Impl *inst = s_instance.load();
    if (!inst)
      return CallNextHookEx(nullptr, nCode, wParam, lParam);

    bool pressed =
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN ? true : false);
    // Handle event asynchronously in instance
    inst->handleEvent(kbd, pressed);
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  // Map the low-level keyboard event to a (codepoint, Key, Modifier, pressed)
  // and invoke the user callback (if set).
  //
  // Note: On Windows the Unicode character computed for a given physical key
  // can differ between the press and the release events due to timing of
  // modifier state and how `ToUnicodeEx` performs translation. For example,
  // pressing a digit key while SHIFT is held may yield a shifted punctuation
  // on the key *press* but the focused application/terminal may observe the
  // unshifted digit as it is delivered (often corresponding more closely to
  // the key *release*). Because of this, consumers that want to reliably
  // capture characters as they appear to applications or terminal/STDIN should
  // prefer handling release events (when `pressed == false`). This listener
  // computes and reports the codepoint for both press and release; callers
  // can choose which event to observe based on their needs.
  void handleEvent(const KBDLLHOOKSTRUCT *kbd, bool pressed) {
    if (!kbd)
      return;

    WORD vk = static_cast<WORD>(kbd->vkCode);
    Key mappedKey = Key::Unknown;
    auto it = vkToKey.find(vk);
    if (it != vkToKey.end())
      mappedKey = it->second;

    // Determine Unicode character (simple BMP handling). ToUnicodeEx can
    // return 1 or 2 WCHARs (surrogate pair), or negative for dead keys.
    BYTE keyboardState[256];
    if (!GetKeyboardState(keyboardState)) {
      // Fall back: no keyboard state; still report key with no codepoint.
      invokeCallback(0, mappedKey, deriveModifiers(), pressed);
      return;
    }

    wchar_t wbuf[4]{0};
    HKL layout = GetKeyboardLayout(0);
    // Use scan code from the hook; use ToUnicodeEx to get the Unicode output.
    int ret = ToUnicodeEx(vk, kbd->scanCode, keyboardState, wbuf,
                          static_cast<int>(sizeof(wbuf) / sizeof(wbuf[0])), 0,
                          layout);

    char32_t codepoint = 0;
    if (ret > 0) {
      if (ret == 1) {
        codepoint = static_cast<char32_t>(wbuf[0]);
      } else if (ret == 2) {
        // Combine surrogate pair
        uint32_t high = static_cast<uint32_t>(wbuf[0]);
        uint32_t low = static_cast<uint32_t>(wbuf[1]);
        // Validate surrogates minimally
        if ((0xD800 <= high && high <= 0xDBFF) &&
            (0xDC00 <= low && low <= 0xDFFF)) {
          codepoint = 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
        } else {
          codepoint = 0; // invalid surrogate sequence
        }
      } else {
        // More than 2 chars -> ignore extras, take first for simplicity
        codepoint = static_cast<char32_t>(wbuf[0]);
      }
    } else {
      // ret == 0 or negative (dead key): no produced character for this
      // simple listener. We ignore dead-key composition for now.
      codepoint = 0;
    }

    // Capture modifiers once and reuse them
    Modifier mods = deriveModifiers();

    // Map to textual key name for logging
    std::string keyName = keyToString(mappedKey);

    // Treat Enter and Backspace as control keys (non-printable). If we pass a
    // non-zero codepoint for these keys the consumer callback will append that
    // control character into the observed string rather than handling the
    // key event (e.g., treating Enter as a terminator). Clear the codepoint
    // so callers can react to the logical key event instead.
    if (mappedKey == Key::Enter || mappedKey == Key::Backspace) {
      codepoint = 0;
    }

    // Handle a couple of platform quirks observed on Windows:
    //  1) Some release events can be delivered multiple times for the same vk.
    //     Debounce rapid duplicate releases to avoid emitting duplicate
    //     characters to the consumer.
    //  2) In some cases the release event can lack a Unicode output (ret == 0).
    //     Cache the last press codepoint and use it as a fallback for the
    //     release so the character stream aligns with what applications see.
    static constexpr std::chrono::milliseconds kReleaseDebounceMs{50};

    if (pressed) {
      // On press, remember the unicode output (if any) for potential use on
      // the paired release event.
      if (codepoint != 0) {
        lastPressCp[vk] = codepoint;
      } else {
        // Clear any stale entry for this vk when press does not produce
        // a unicode character (e.g., modifiers).
        lastPressCp.erase(vk);
      }
    } else {
      // On release, prefer using the cached press codepoint if the release
      // did not produce a unicode character. This helps align the character
      // stream with what applications/terminals see. Do not override control
      // keys (Enter/Backspace) which are intentionally cleared above.
      if (codepoint == 0 && mappedKey != Key::Enter &&
          mappedKey != Key::Backspace) {
        auto cpIt = lastPressCp.find(vk);
        if (cpIt != lastPressCp.end()) {
          codepoint = cpIt->second;
          if (output_debug_enabled()) {
            TYPR_IO_LOG_DEBUG(
                "Listener (Windows): using last-press cp=%u for release vk=%u",
                static_cast<unsigned>(codepoint), static_cast<unsigned>(vk));
          }
        }
      }

      // On release, debounce rapid duplicates coming from the system.
      auto now = std::chrono::steady_clock::now();
      auto rtIt = lastReleaseTime.find(vk);
      auto sigIt = lastReleaseSig.find(vk);
      if (rtIt != lastReleaseTime.end() && sigIt != lastReleaseSig.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - rtIt->second);
        if (elapsed < kReleaseDebounceMs && sigIt->second.first == codepoint &&
            sigIt->second.second == mods) {
          if (output_debug_enabled()) {
            TYPR_IO_LOG_DEBUG("Listener (Windows): ignoring duplicate release "
                              "(same cp+mods) for vk=%u key=%s cp=%u mods=%u",
                              static_cast<unsigned>(vk), keyName.c_str(),
                              static_cast<unsigned>(codepoint),
                              static_cast<unsigned>(mods));
          }
          // Update timestamp & signature so subsequent quick duplicates remain
          // debounced.
          lastReleaseTime[vk] = now;
          lastReleaseSig[vk] = std::make_pair(codepoint, mods);
          // We've handled the release; clear cached press cp so it won't be
          // accidentally reused later.
          lastPressCp.erase(vk);
          return;
        }
      }

      // Not a duplicate matching the last cp+mods -> record the new
      // signature/time.
      lastReleaseTime[vk] = now;
      lastReleaseSig[vk] = std::make_pair(codepoint, mods);

      // We've handled the release, clear the cached press codepoint so we
      // don't accidentally reuse it for future, unrelated events.
      lastPressCp.erase(vk);
    }

    invokeCallback(codepoint, mappedKey, mods, pressed);

    TYPR_IO_LOG_DEBUG(
        "Listener (Windows) %s: vk=%u sc=%u flags=%u key=%s cp=%u mods=%u",
        pressed ? "press" : "release", static_cast<unsigned>(vk),
        static_cast<unsigned>(kbd->scanCode), static_cast<unsigned>(kbd->flags),
        keyName.c_str(), static_cast<unsigned>(codepoint),
        static_cast<unsigned>(mods));
  }

  /**
   * @internal
   * @brief Derive the current modifier bitmask via Win32 `GetKeyState`.
   *
   * Tests Shift/Ctrl/Alt/Super and CapsLock state and returns a
   * `typr::io::Modifier` bitmask representing the active modifiers.
   *
   * @return Modifier Active modifier bitmask.
   */
  Modifier deriveModifiers() const {
    Modifier mods = Modifier::None;
    if (GetKeyState(VK_SHIFT) & 0x8000)
      mods = mods | Modifier::Shift;
    if (GetKeyState(VK_CONTROL) & 0x8000)
      mods = mods | Modifier::Ctrl;
    if (GetKeyState(VK_MENU) & 0x8000)
      mods = mods | Modifier::Alt;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
      mods = mods | Modifier::Super;
    if (GetKeyState(VK_CAPITAL) & 0x0001)
      mods = mods | Modifier::CapsLock;
    return mods;
  }

  /**
   * @internal
   * @brief Safely invoke the user-provided callback.
   *
   * Copies the stored callback under `cbMutex` and then invokes it outside
   * the lock to avoid holding internal mutexes while calling user code.
   *
   * @param cp Unicode codepoint produced by the event (0 if none).
   * @param k Logical Key for the event.
   * @param mods Modifier bitmask at time of event.
   * @param pressed True for key press, false for release.
   */
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

  /**
   * @internal
   * @brief Worker thread main that installs the low-level keyboard hook and
   * processes messages until WM_QUIT is received.
   *
   * Installs the WH_KEYBOARD_LL hook, sets the ready flag on successful
   * installation, and runs a standard Windows message loop dispatching
   * messages to the hook. On shutdown it uninstalls the hook and clears
   * shared instance state.
   */
  void threadMain() {
    // Save thread id so stop() can post WM_QUIT
    threadId.store(GetCurrentThreadId());
    // Register instance for the hookproc
    s_instance.store(this);

    hook = SetWindowsHookEx(WH_KEYBOARD_LL, &Impl::lowLevelKeyboardProc,
                            GetModuleHandle(nullptr), 0);
    if (hook == nullptr) {
      // Failed to create hook; clear instance and exit thread.
      s_instance.store(nullptr);
      threadId.store(0);
      running.store(false);
      ready.store(false);
      TYPR_IO_LOG_ERROR("Listener (Windows): SetWindowsHookEx failed");
      return;
    }

    // Hook installed successfully
    ready.store(true);
    TYPR_IO_LOG_INFO("Listener (Windows): low-level keyboard hook installed");

    // Standard message loop (blocks on GetMessage; WM_QUIT ends it).
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    // Cleanup
    UnhookWindowsHookEx(hook);
    hook = nullptr;
    s_instance.store(nullptr);
    threadId.store(0);
    ready.store(false);
  }
};

// Static instance pointer definition
std::atomic<Listener::Impl *> Listener::Impl::s_instance{nullptr};

// OutputListener public API wrappers

TYPR_IO_API Listener::Listener() : m_impl(std::make_unique<Impl>()) {}
TYPR_IO_API Listener::~Listener() { stop(); }
TYPR_IO_API Listener::Listener(Listener &&) noexcept = default;
TYPR_IO_API Listener &Listener::operator=(Listener &&) noexcept = default;

TYPR_IO_API bool Listener::start(Callback cb) {
  TYPR_IO_LOG_DEBUG("Listener::start() called (Windows)");
  return m_impl ? m_impl->start(std::move(cb)) : false;
}

TYPR_IO_API void Listener::stop() {
  TYPR_IO_LOG_DEBUG("Listener::stop() called (Windows)");
  if (m_impl)
    m_impl->stop();
}

TYPR_IO_API bool Listener::isListening() const {
  TYPR_IO_LOG_DEBUG("Listener::isListening() called (Windows)");
  return m_impl ? m_impl->isRunning() : false;
}

} // namespace typr::io::keyboard

#endif // _WIN32
