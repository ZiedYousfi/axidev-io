#if defined(__linux__)

#include <typr-io/listener.hpp>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace typr::io {

namespace {
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

/**
 * OutputListener implementation for X11 using XInput2 raw events.
 *
 * Notes / limitations (best-effort, intentionally simple):
 * - Uses XI_RawKeyPress / XI_RawKeyRelease to observe global key events.
 * - Maps X keycodes -> Key enum via a layout-aware scan at startup.
 * - Attempts to derive a produced Unicode codepoint for common ASCII/BMP
 *   keys by using XkbKeycodeToKeysym and simple heuristics.
 * - Modifier state is derived from XKB `XkbGetState` and mapped to the
 *   `Modifier` enum.
 * - Does not attempt to implement full IME / dead-key / complex input
 *   composition handling. This is intentionally lightweight.
 */

struct Listener::Impl {
  Impl() : running(false), xiOpcode(-1), dpy(nullptr) {}
  ~Impl() { stop(); }

  bool start(Callback cb) {
    std::lock_guard<std::mutex> lk(cbMutex);
    if (running.load())
      return false;
    callback = std::move(cb);
    running.store(true);
    ready.store(false);
    worker = std::thread(&Impl::threadMain, this);

    // Wait briefly for the listener to initialize or fail
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
    if (worker.joinable())
      worker.join();

    // Cleanup display if still open
    if (dpy) {
      // Deselect events (best-effort) then close display.
      // It's possible that the display was already closed by threadMain on
      // exit.
      XCloseDisplay(dpy);
      dpy = nullptr;
    }

    {
      std::lock_guard<std::mutex> lk(cbMutex);
      callback = nullptr;
    }
    keyCodeToKey.clear();
  }

  bool isRunning() const { return running.load(); }

private:
  // Main thread: open display, register XI2 raw key events and process them.
  void threadMain() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) {
      running.store(false);
      ready.store(false);
      if (output_debug_enabled()) {
        fprintf(stderr,
                "[typr-backend] Listener (X11): XOpenDisplay() failed\n");
      }
      return;
    }

    // Ensure XInput extension is present
    int event_base, error_base;
    if (!XQueryExtension(dpy, "XInputExtension", &xiOpcode, &event_base,
                         &error_base)) {
      // No XInput extension available
      XCloseDisplay(dpy);
      dpy = nullptr;
      running.store(false);
      ready.store(false);
      if (output_debug_enabled()) {
        fprintf(stderr, "[typr-backend] Listener (X11): XInput extension "
                        "not available\n");
      }
      return;
    }

    // Check XI2 version (we need XI2)
    int xiMajor = 2, xiMinor = 2;
    if (XIQueryVersion(dpy, &xiMajor, &xiMinor) != Success) {
      // XI2 not available
      XCloseDisplay(dpy);
      dpy = nullptr;
      running.store(false);
      ready.store(false);
      if (output_debug_enabled()) {
        fprintf(stderr, "[typr-backend] Listener (X11): XI2 not "
                        "available (XIQueryVersion failed)\n");
      }
      return;
    }

    // Initialize per-display keycode -> Key mapping
    initKeyMap();

    // Select raw key events on the root window
    Window root = DefaultRootWindow(dpy);
    unsigned char mask[XIMaskLen(XI_RawKeyRelease)];
    std::memset(mask, 0, sizeof(mask));
    XISetMask(mask, XI_RawKeyPress);
    XISetMask(mask, XI_RawKeyRelease);

    XIEventMask evmask;
    evmask.deviceid = XIAllMasterDevices;
    evmask.mask_len = sizeof(mask);
    evmask.mask = mask;

    XISelectEvents(dpy, root, &evmask, 1);
    XFlush(dpy);

    // Event selection succeeded; mark as ready and optionally log
    ready.store(true);
    if (output_debug_enabled()) {
      fprintf(stderr, "[typr-backend] Listener (X11): registered for "
                      "XI_RawKey events\n");
    }

    // Polling event loop (keeps it simple and avoid blocking shutdown issues)
    while (running.load()) {
      // Process all pending events
      while (XPending(dpy) > 0 && running.load()) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == GenericEvent && ev.xgeneric.serial) {
          XGenericEventCookie *cookie = &ev.xcookie;
          if (cookie->type == GenericEvent && cookie->extension == xiOpcode) {
            if (XGetEventData(dpy, cookie)) {
              if (cookie->evtype == XI_RawKeyPress ||
                  cookie->evtype == XI_RawKeyRelease) {
                handleRawKeyEvent(reinterpret_cast<XIEvent *>(cookie->data));
              }
              XFreeEventData(dpy, cookie);
            }
          }
        }
      }
      // Small sleep to avoid burning CPU if no events
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Clean up selection
    XIEventMask clearMask;
    std::memset(mask, 0, sizeof(mask));
    clearMask.deviceid = XIAllMasterDevices;
    clearMask.mask_len = sizeof(mask);
    clearMask.mask = mask;
    XISelectEvents(dpy, root, &clearMask, 1);
    XFlush(dpy);

    // Mark as no longer ready and close display
    ready.store(false);
    // Close display
    XCloseDisplay(dpy);
    dpy = nullptr;
  }

  // Handle a Raw event (XI_RawKeyPress / XI_RawKeyRelease)
  void handleRawKeyEvent(XIEvent *xiev) {
    // XI_RawEvent is the actual underlying structure for raw key events.
    if (!xiev)
      return;

    // raw events are represented as XI_RawKeyPress / XI_RawKeyRelease
    if (xiev->evtype != XI_RawKeyPress && xiev->evtype != XI_RawKeyRelease)
      return;

    XI_RawEvent *rev = reinterpret_cast<XI_RawEvent *>(xiev);
    int keycode = rev->detail; // X keycode (hardware keycode)
    bool pressed = (rev->evtype == XI_RawKeyPress);

    // Query XKB state to determine modifiers and shift state
    XkbStateRec kbState;
    if (XkbGetState(dpy, XkbUseCoreKbd, &kbState) != Success) {
      // On failure, set to zero
      std::memset(&kbState, 0, sizeof(kbState));
    }

    Modifier mods = Modifier::None;
    if (kbState.mods & XkbShiftMask)
      mods = mods | Modifier::Shift;
    if (kbState.mods & XkbCtrlMask)
      mods = mods | Modifier::Ctrl;
    if (kbState.mods & XkbMod1Mask)
      mods = mods | Modifier::Alt; // typically Mod1 == Alt
    if (kbState.mods & XkbMod4Mask)
      mods = mods | Modifier::Super; // typically Mod4 == Super
    if (kbState.locked_mods & XkbLockMask)
      mods = mods | Modifier::CapsLock;

    // Map keycode to a Key enum if possible
    Key mappedKey = Key::Unknown;
    {
      auto it = keyCodeToKey.find(keycode);
      if (it != keyCodeToKey.end()) {
        mappedKey = it->second;
      } else {
        // fallback: try to derive from keysym name
        KeySym ks = XkbKeycodeToKeysym(dpy, static_cast<KeyCode>(keycode), 0,
                                       (kbState.mods & XkbShiftMask) ? 1 : 0);
        if (ks != NoSymbol) {
          const char *ksName = XKeysymToString(ks);
          if (ksName) {
            mappedKey = stringToKey(std::string(ksName));
          }
        }
      }
    }

    // Derive a best-effort codepoint: prefer ASCII/BMP printable characters
    char32_t codepoint = 0;
    KeySym ks = XkbKeycodeToKeysym(dpy, static_cast<KeyCode>(keycode), 0,
                                   (kbState.mods & XkbShiftMask) ? 1 : 0);
    if (ks != NoSymbol) {
      // For simple ASCII range
      if ((ks >= XK_space && ks <= XK_asciitilde)) {
        unsigned long raw = static_cast<unsigned long>(ks);
        if (std::isprint(static_cast<int>(raw))) {
          codepoint = static_cast<char32_t>(raw);
        }
      } else {
        // For a few named keys map them to representative characters when
        // applicable (e.g. space, tab, Return)
        if (ks == XK_space) {
          codepoint = U' ';
        } else if (ks == XK_Tab) {
          codepoint = U'\t';
        } else if (ks == XK_Return) {
          codepoint = U'\n';
        } else {
          // Leave codepoint as 0 for non-printable keys
          codepoint = 0;
        }
      }
    }

    // Invoke callback (copy under lock)
    Callback cbCopy;
    {
      std::lock_guard<std::mutex> lk(cbMutex);
      cbCopy = callback;
    }
    if (cbCopy) {
      cbCopy(codepoint, mapped, mods, pressed);
    }

    // Debug logging (enabled by default for testing; disable with
    // TYPR_OSK_DEBUG_BACKEND=0)
    if (output_debug_enabled()) {
      std::string keyName = keyToString(mapped);
      fprintf(stderr,
              "[typr-backend] Listener (X11) %s: keycode=%d key=%s "
              "keysym=%lu cp=%u mods=%u\n",
              pressed ? "press" : "release", keycode, keyName.c_str(),
              static_cast<unsigned long>(ks), static_cast<unsigned>(codepoint),
              static_cast<unsigned>(mods));
    }
  }

  // Build a reverse mapping from keycode -> Key by scanning available keycodes
  // and using the current keyboard layout via Xkb.
  void initKeyMap() {
    keyCodeToKey.clear();
    if (!dpy) {
      // We need a temporary display to probe keycodes
      Display *tmp = XOpenDisplay(nullptr);
      if (!tmp)
        return;
      populateKeyMapFromDisplay(tmp);
      XCloseDisplay(tmp);
    } else {
      populateKeyMapFromDisplay(dpy);
    }

    // Ensure some canonical fallbacks are present (space, enter, tab, arrows,
    // etc.)
    addFallback(Key::Space, XK_space);
    addFallback(Key::Enter, XK_Return);
    addFallback(Key::Tab, XK_Tab);
    addFallback(Key::Backspace, XK_BackSpace);
    addFallback(Key::Delete, XK_Delete);
    addFallback(Key::Escape, XK_Escape);
    addFallback(Key::Left, XK_Left);
    addFallback(Key::Right, XK_Right);
    addFallback(Key::Up, XK_Up);
    addFallback(Key::Down, XK_Down);
    addFallback(Key::Home, XK_Home);
    addFallback(Key::End, XK_End);
    addFallback(Key::PageUp, XK_Page_Up);
    addFallback(Key::PageDown, XK_Page_Down);

    // Modifiers
    addFallback(Key::ShiftLeft, XK_Shift_L);
    addFallback(Key::ShiftRight, XK_Shift_R);
    addFallback(Key::CtrlLeft, XK_Control_L);
    addFallback(Key::CtrlRight, XK_Control_R);
    addFallback(Key::AltLeft, XK_Alt_L);
    addFallback(Key::AltRight, XK_Alt_R);
    addFallback(Key::SuperLeft, XK_Super_L);
    addFallback(Key::SuperRight, XK_Super_R);
    addFallback(Key::CapsLock, XK_Caps_Lock);

    // Punctuation and punctuation-like keys (attempt to canonicalize common
    // ones)
    addFallback(Key::Comma, XK_comma);
    addFallback(Key::Period, XK_period);
    addFallback(Key::Slash, XK_slash);
    addFallback(Key::Backslash, XK_backslash);
    addFallback(Key::Semicolon, XK_semicolon);
    addFallback(Key::Apostrophe, XK_apostrophe);
    addFallback(Key::Minus, XK_minus);
    addFallback(Key::Equal, XK_equal);
    addFallback(Key::Grave, XK_grave);
    addFallback(Key::LeftBracket, XK_bracketleft);
    addFallback(Key::RightBracket, XK_bracketright);
  }

  // Populate keyCodeToKey by iterating display keycodes and mapping via keysym
  void populateKeyMapFromDisplay(Display *display) {
    if (!display)
      return;

    int minKC, maxKC;
    XDisplayKeycodes(display, &minKC, &maxKC);
    for (int kc = minKC; kc <= maxKC; ++kc) {
      // Try both unshifted and shifted levels to discover printable mappings
      for (int level = 0; level <= 1; ++level) {
        KeySym ks =
            XkbKeycodeToKeysym(display, static_cast<KeyCode>(kc), 0, level);
        if (ks == NoSymbol)
          continue;
        const char *ksName = XKeysymToString(ks);
        if (!ksName)
          continue;
        // Convert keysym name (e.g., \"a\", \"space\", \"Return\") into Key
        Key mapped = stringToKey(std::string(ksName));
        if (mapped != Key::Unknown) {
          // Prefer the first discovered mapping for a given Key
          if (std::find_if(keyCodeToKey.begin(), keyCodeToKey.end(),
                           [&](auto &p) { return p.second == mapped; }) ==
              keyCodeToKey.end()) {
            keyCodeToKey[kc] = mapped;
          }
        } else {
          // For simple ASCII characters, map the character itself
          if ((ks >= XK_space && ks <= XK_asciitilde) &&
              std::isprint(static_cast<int>(ks))) {
            std::string s(1, static_cast<char>(ks));
            Key k = stringToKey(s);
            if (k != Key::Unknown) {
              if (std::find_if(keyCodeToKey.begin(), keyCodeToKey.end(),
                               [&](auto &p) { return p.second == k; }) ==
                  keyCodeToKey.end()) {
                keyCodeToKey[kc] = k;
              }
            }
          }
        }
      }
    }
  }

  // Helper to add fallback mapping from keysym to keycode -> Key mapping
  void addFallback(Key target, KeySym sym) {
    if (!dpy) {
      Display *tmp = XOpenDisplay(nullptr);
      if (!tmp)
        return;
      KeyCode kc = XKeysymToKeycode(tmp, sym);
      if (kc != 0) {
        if (keyCodeToKey.find(kc) == keyCodeToKey.end())
          keyCodeToKey[kc] = target;
      }
      XCloseDisplay(tmp);
    } else {
      KeyCode kc = XKeysymToKeycode(dpy, sym);
      if (kc != 0) {
        if (keyCodeToKey.find(kc) == keyCodeToKey.end())
          keyCodeToKey[kc] = target;
      }
    }
  }

  std::thread worker;
  std::atomic_bool running;
  std::atomic_bool ready{false};
  std::mutex cbMutex;
  Callback callback;
  // X-related state
  Display *dpy;
  int xiOpcode;

  // Reverse mapping: X keycode -> Key
  std::unordered_map<int, Key> keyCodeToKey;
};
// Listener public wrappers

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

} // namespace typr::io

#endif // __linux__
