/**
 * @file keyboard/listener/listener_linux.cpp
 * @brief Linux/libinput implementation of axidev::io::keyboard::Listener.
 *
 * Provides a libinput-based global keyboard event listener that translates
 * low-level input events into logical keys and Unicode codepoints using
 * xkbcommon. The implementation handles device discovery, event translation,
 * and invokes the public Listener callback on observed events.
 */
#if defined(__linux__)

#include <axidev-io/keyboard/listener.hpp>
#include <axidev-io/log.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <libinput.h>
#include <libudev.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "keyboard/common/linux_layout.hpp"

namespace axidev::io::keyboard {

namespace {

/**
 * @internal
 * @brief libinput callbacks for opening/closing device nodes.
 *
 * These callbacks are invoked by libinput when it needs to open or close a
 * device node. They mirror the interface expected by libinput and translate
 * the returned file descriptor into the negative errno value expected by the
 * library on failure.
 *
 * Note: the signatures match the libinput API and must remain compatible.
 *
 * @param path Device path to open (for `open_restricted`).
 * @param flags Flags passed to open(2).
 * @return On success `open_restricted` returns a file descriptor; on failure
 *         it returns a negative errno value as required by libinput.
 */
static int open_restricted(const char *path, int flags, void *) {
  int fd = ::open(path, flags);
  return fd < 0 ? -errno : fd;
}

/**
 * @internal
 * @brief Close a file descriptor previously opened by `open_restricted`.
 *
 * This wraps the platform close operation so that libinput can abstract
 * device open/close across backends.
 *
 * @param fd File descriptor to close.
 */
static void close_restricted(int fd, void *) { ::close(fd); }

/**
 * @internal
 * @brief Interface structure provided to libinput to perform device open/close.
 *
 * This structure maps libinput's required callbacks to the local
 * implementations above (`open_restricted` / `close_restricted`).
 */
static const struct libinput_interface kInterface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

} // namespace

/**
 * @internal
 * @brief PIMPL implementation for `axidev::io::keyboard::Listener`.
 *
 * This structure encapsulates the platform-specific state required to
 * implement the Listener, including the worker thread that polls libinput,
 * internal synchronization primitives and the callback forwarding bridge.
 *
 * These members and methods are internal implementation details and are not
 * part of the public API; they may change without notice.
 */
struct Listener::Impl {
  Impl() = default;
  ~Impl() { stop(); }

  /**
   * @internal
   * @brief Start the implementation worker thread and store the callback.
   *
   * The provided callback is stored under `cbMutex` to avoid races with the
   * worker thread. This method starts a background thread which performs
   * device discovery and event processing; it waits briefly for the worker to
   * report readiness and returns whether initialization succeeded.
   *
   * @param cb Callback that will be invoked for each observed event.
   * @return true on success and when the worker becomes ready.
   */
  bool start(Callback cb) {
    std::lock_guard<std::mutex> lk(cbMutex);
    if (running.load())
      return false;

    callback = std::move(cb);
    running.store(true);
    ready.store(false);
    worker = std::thread(&Impl::threadMain, this);

    // Wait (up to ~200ms) for initialization
    for (int i = 0; i < 40; ++i) {
      if (!running.load())
        return false;
      if (ready.load())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool ok = ready.load();
    AXIDEV_IO_LOG_DEBUG("Listener (Linux/libinput): start result=%u",
                      static_cast<unsigned>(ok));
    return ok;
  }

  /**
   * @internal
   * @brief Stop the worker thread and clear the stored callback.
   *
   * Safe to call from any thread. If a worker is running it will be asked to
   * stop and joined; the callback pointer is cleared under `cbMutex` to prevent
   * further invocations.
   */
  void stop() {
    if (!running.load())
      return;

    running.store(false);
    if (worker.joinable())
      worker.join();

    // Clear callback under lock
    {
      std::lock_guard<std::mutex> lk(cbMutex);
      callback = nullptr;
    }

    AXIDEV_IO_LOG_INFO("Listener (Linux/libinput): stopped");
  }

  /**
   * @internal
   * @brief Query whether the implementation's worker thread is active.
   * @return true when the worker thread is running.
   */
  bool isRunning() const { return running.load(); }

private:
  /**
   * @internal
   * @brief Worker thread main loop.
   *
   * Initializes udev/libinput/xkb state, performs device enumeration, and
   * enters the event loop. Events are translated into logical `Key` values and
   * Unicode codepoints which are forwarded to the user-provided callback.
   *
   * This method runs on a dedicated background thread and must not be invoked
   * directly by user code.
   */
  void threadMain() {
    struct udev *udev = udev_new();
    if (!udev) {
      AXIDEV_IO_LOG_ERROR("Listener (Linux/libinput): udev_new() failed");
      running.store(false);
      ready.store(false);
      return;
    }

    li = libinput_udev_create_context(&kInterface, nullptr, udev);
    if (!li) {
      AXIDEV_IO_LOG_ERROR(
          "Listener (Linux/libinput): libinput_udev_create_context() failed");
      udev_unref(udev);
      running.store(false);
      ready.store(false);
      return;
    }

    if (libinput_udev_assign_seat(li, "seat0") < 0) {
      AXIDEV_IO_LOG_ERROR(
          "Listener (Linux/libinput): libinput_udev_assign_seat() failed. "
          "Are you in the 'input' group or running with necessary privileges?");
      libinput_unref(li);
      udev_unref(udev);
      running.store(false);
      ready.store(false);
      return;
    }

    // Initialize xkbcommon context / keymap / state for translating keycodes
    xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkbCtx) {
      AXIDEV_IO_LOG_ERROR("Listener (Linux/libinput): xkb_context_new() failed");
      libinput_unref(li);
      udev_unref(udev);
      running.store(false);
      ready.store(false);
      return;
    }
    // Try to initialize XKB keymap names from environment variables or a
    // system configuration file (/etc/default/keyboard). This helps ensure the
    // keymap used for translating evdev keycodes matches the user's actual
    // layout (e.g. non-QWERTY layouts).
    const auto detected = axidev::io::keyboard::detail::detectXkbRuleNames();

    const std::string &rulesStr = detected.rules;
    const std::string &modelStr = detected.model;
    const std::string &layoutStr = detected.layout;
    const std::string &variantStr = detected.variant;
    const std::string &optionsStr = detected.options;

    struct xkb_rule_names names = {nullptr, nullptr, nullptr, nullptr, nullptr};
    std::string dbg;
    if (!rulesStr.empty()) {
      names.rules = rulesStr.c_str();
      dbg += "rules=" + rulesStr + " ";
    }
    if (!modelStr.empty()) {
      names.model = modelStr.c_str();
      dbg += "model=" + modelStr + " ";
    }
    if (!layoutStr.empty()) {
      names.layout = layoutStr.c_str();
      dbg += "layout=" + layoutStr + " ";
    }
    if (!variantStr.empty()) {
      names.variant = variantStr.c_str();
      dbg += "variant=" + variantStr + " ";
    }
    if (!optionsStr.empty()) {
      names.options = optionsStr.c_str();
      dbg += "options=" + optionsStr + " ";
    }
    if (!dbg.empty()) {
      AXIDEV_IO_LOG_DEBUG("Listener (Linux/libinput): xkb names: %s",
                        dbg.c_str());
    }

    xkbKeymap =
        xkb_keymap_new_from_names(xkbCtx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkbKeymap) {
      AXIDEV_IO_LOG_ERROR(
          "Listener (Linux/libinput): xkb_keymap_new_from_names() failed");
      xkb_context_unref(xkbCtx);
      libinput_unref(li);
      udev_unref(udev);
      running.store(false);
      ready.store(false);
      return;
    }
    xkbState = xkb_state_new(xkbKeymap);
    if (!xkbState) {
      AXIDEV_IO_LOG_ERROR("Listener (Linux/libinput): xkb_state_new() failed");
      xkb_keymap_unref(xkbKeymap);
      xkb_context_unref(xkbCtx);
      libinput_unref(li);
      udev_unref(udev);
      running.store(false);
      ready.store(false);
      return;
    }

    ready.store(true);
    AXIDEV_IO_LOG_INFO("Listener (Linux/libinput): Monitoring started");

    int fd = libinput_get_fd(li);
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};

    while (running.load()) {
      int ret = poll(&pfd, 1, 100);
      if (ret > 0 && (pfd.revents & POLLIN)) {
        libinput_dispatch(li);
        struct libinput_event *ev;
        while ((ev = libinput_get_event(li))) {
          enum libinput_event_type t = libinput_event_get_type(ev);
          if (t == LIBINPUT_EVENT_KEYBOARD_KEY) {
            handleKeyEvent(libinput_event_get_keyboard_event(ev));
          }
          libinput_event_destroy(ev);
        }
      }
      // small sleep if poll timed out or no events - helps on idle loops
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    if (xkbState) {
      xkb_state_unref(xkbState);
      xkbState = nullptr;
    }
    if (xkbKeymap) {
      xkb_keymap_unref(xkbKeymap);
      xkbKeymap = nullptr;
    }
    if (xkbCtx) {
      xkb_context_unref(xkbCtx);
      xkbCtx = nullptr;
    }
    if (li) {
      libinput_unref(li);
      li = nullptr;
    }
    udev_unref(udev);

    ready.store(false);
  }

  void handleKeyEvent(struct libinput_event_keyboard *kev) {
    if (!kev)
      return;

    uint32_t keycode = libinput_event_keyboard_get_key(kev);
    bool pressed = libinput_event_keyboard_get_key_state(kev) ==
                   LIBINPUT_KEY_STATE_PRESSED;

    // libinput provides evdev keycodes; xkbcommon expects keycodes offset by 8
    xkb_keycode_t xkbKey = static_cast<xkb_keycode_t>(keycode + 8);

    // Update xkb state
    xkb_state_update_key(xkbState, xkbKey, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    // Determine keysym and unicode codepoint (best-effort)
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState, xkbKey);
    char32_t codepoint = 0;
    if (pressed) {
      // Compute codepoint for this key on press. Only stash printable
      // characters (non-control). Control characters (e.g., Enter/Backspace)
      // should not be delivered as a non-zero cp; they are handled via Key.
      char32_t cp = static_cast<char32_t>(xkb_keysym_to_utf32(sym));
      // Treat as printable if >= 0x20 and not DEL (0x7F). This is a simple,
      // conservative heuristic that covers typical keyboard input.
      if (cp >= 0x20 && cp != 0x7F) {
        pendingCodepoints[keycode] = cp;
      } else {
        // Ensure no stale mapping remains for this key
        auto it = pendingCodepoints.find(keycode);
        if (it != pendingCodepoints.end())
          pendingCodepoints.erase(it);
      }
    } else {
      // Deliver any codepoint we previously computed at press time for this
      // key. This ensures callbacks observing only key-release events still
      // receive the character that was generated when the key was pressed.
      auto it = pendingCodepoints.find(keycode);
      if (it != pendingCodepoints.end()) {
        codepoint = it->second;
        pendingCodepoints.erase(it);
      } else {
        codepoint = 0;
      }
    }

    // Map keysym -> Key
    Key mapped = mapKeysymToKey(sym);

    // Extract modifiers via xkb state
    Modifier mods = Modifier::None;
    if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE))
      mods = mods | Modifier::Shift;
    if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE))
      mods = mods | Modifier::Ctrl;
    if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE))
      mods = mods | Modifier::Alt;
    if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_LOGO,
                                     XKB_STATE_MODS_EFFECTIVE))
      mods = mods | Modifier::Super;
    // CapsLock is typically exposed as the "Lock" modifier name
    if (xkb_state_mod_name_is_active(xkbState, "Lock",
                                     XKB_STATE_MODS_EFFECTIVE))
      mods = mods | Modifier::CapsLock;

    // Copy/dispatch callback under lock
    Callback cbCopy;
    {
      std::lock_guard<std::mutex> lk(cbMutex);
      cbCopy = callback;
    }
    if (cbCopy)
      cbCopy(codepoint, mapped, mods, pressed);

    // Debug logging
    {
      std::string kname = keyToString(mapped);
      AXIDEV_IO_LOG_DEBUG("Listener (Linux/libinput) %s: evdev=%u keysym=%u "
                        "key=%s cp=%u mods=0x%02x",
                        pressed ? "press" : "release", keycode,
                        static_cast<unsigned>(sym), kname.c_str(),
                        static_cast<unsigned>(codepoint),
                        static_cast<int>(static_cast<uint8_t>(mods)));
    }
  }

  Key mapKeysymToKey(xkb_keysym_t sym) {
    // Quick alphabetic mapping (lowercase and uppercase)
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
      return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_a));
    }
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) {
      return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_A));
    }

    // Top-row numbers
    if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) {
      return static_cast<Key>(static_cast<int>(Key::Num0) + (sym - XKB_KEY_0));
    }

    // Function keys
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F20) {
      return static_cast<Key>(static_cast<int>(Key::F1) + (sym - XKB_KEY_F1));
    }

    // Direct mappings for common control keys
    switch (sym) {
    case XKB_KEY_Return:
      return Key::Enter;
    case XKB_KEY_BackSpace:
      return Key::Backspace;
    case XKB_KEY_space:
      return Key::Space;
    case XKB_KEY_Tab:
      return Key::Tab;
    case XKB_KEY_Escape:
      return Key::Escape;
    case XKB_KEY_Left:
      return Key::Left;
    case XKB_KEY_Right:
      return Key::Right;
    case XKB_KEY_Up:
      return Key::Up;
    case XKB_KEY_Down:
      return Key::Down;
    case XKB_KEY_Home:
      return Key::Home;
    case XKB_KEY_End:
      return Key::End;
    case XKB_KEY_Page_Up:
      return Key::PageUp;
    case XKB_KEY_Page_Down:
      return Key::PageDown;
    case XKB_KEY_Delete:
      return Key::Delete;
    case XKB_KEY_Insert:
      return Key::Insert;
    // Numpad
    case XKB_KEY_KP_Divide:
      return Key::NumpadDivide;
    case XKB_KEY_KP_Multiply:
      return Key::NumpadMultiply;
    case XKB_KEY_KP_Subtract:
      return Key::NumpadMinus;
    case XKB_KEY_KP_Add:
      return Key::NumpadPlus;
    case XKB_KEY_KP_Enter:
      return Key::NumpadEnter;
    case XKB_KEY_KP_Decimal:
      return Key::NumpadDecimal;
    case XKB_KEY_KP_0:
      return Key::Numpad0;
    case XKB_KEY_KP_1:
      return Key::Numpad1;
    case XKB_KEY_KP_2:
      return Key::Numpad2;
    case XKB_KEY_KP_3:
      return Key::Numpad3;
    case XKB_KEY_KP_4:
      return Key::Numpad4;
    case XKB_KEY_KP_5:
      return Key::Numpad5;
    case XKB_KEY_KP_6:
      return Key::Numpad6;
    case XKB_KEY_KP_7:
      return Key::Numpad7;
    case XKB_KEY_KP_8:
      return Key::Numpad8;
    case XKB_KEY_KP_9:
      return Key::Numpad9;
    // Common punctuation
    case XKB_KEY_comma:
      return Key::Comma;
    case XKB_KEY_period:
      return Key::Period;
    case XKB_KEY_slash:
      return Key::Slash;
    case XKB_KEY_backslash:
      return Key::Backslash;
    case XKB_KEY_semicolon:
      return Key::Semicolon;
    case XKB_KEY_apostrophe:
      return Key::Apostrophe;
    case XKB_KEY_minus:
      return Key::Minus;
    case XKB_KEY_equal:
      return Key::Equal;
    case XKB_KEY_grave:
      return Key::Grave;
    case XKB_KEY_bracketleft:
      return Key::LeftBracket;
    case XKB_KEY_bracketright:
      return Key::RightBracket;
    default:
      break;
    }

    // Try to map using the keysym name -> stringToKey as a best-effort fallback
    char name[64] = {0};
    if (xkb_keysym_get_name(sym, name, sizeof(name)) > 0) {
      Key mapped = stringToKey(std::string(name));
      if (mapped != Key::Unknown)
        return mapped;
    }

    return Key::Unknown;
  }

  std::thread worker;
  std::atomic_bool running{false};
  std::atomic_bool ready{false};
  std::mutex cbMutex;
  Callback callback;

  // Store unicode codepoints computed at key-press time so they can be
  // delivered on key-release events.
  std::unordered_map<uint32_t, char32_t> pendingCodepoints;

  struct libinput *li = nullptr;
  struct xkb_context *xkbCtx = nullptr;
  struct xkb_keymap *xkbKeymap = nullptr;
  struct xkb_state *xkbState = nullptr;
};

// Public wrappers
Listener::Listener() : m_impl(std::make_unique<Impl>()) {}
Listener::~Listener() { stop(); }
Listener::Listener(Listener &&) noexcept = default;
Listener &Listener::operator=(Listener &&) noexcept = default;

bool Listener::start(Callback cb) {
  AXIDEV_IO_LOG_DEBUG("Listener::start() called (Linux/libinput)");
  return m_impl ? m_impl->start(std::move(cb)) : false;
}

void Listener::stop() {
  AXIDEV_IO_LOG_DEBUG("Listener::stop() called (Linux/libinput)");
  if (m_impl)
    m_impl->stop();
}

bool Listener::isListening() const {
  AXIDEV_IO_LOG_DEBUG("Listener::isListening() called (Linux/libinput)");
  return m_impl ? m_impl->isRunning() : false;
}

} // namespace axidev::io::keyboard

#endif // __linux__
