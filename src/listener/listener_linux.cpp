#if defined(__linux__)

#include <typr-io/listener.hpp>
#include <typr-io/log.hpp>

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

namespace typr::io {

namespace {

// libinput callbacks for opening/closing device nodes. This matches the
// examples from libinput's documentation.
static int open_restricted(const char *path, int flags, void *) {
  int fd = ::open(path, flags);
  return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *) { ::close(fd); }

static const struct libinput_interface kInterface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

} // namespace

struct Listener::Impl {
  Impl() = default;
  ~Impl() { stop(); }

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
    TYPR_IO_LOG_DEBUG("Listener (Linux/libinput): start result=%u",
                      static_cast<unsigned>(ok));
    return ok;
  }

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

    TYPR_IO_LOG_INFO("Listener (Linux/libinput): stopped");
  }

  bool isRunning() const { return running.load(); }

private:
  void threadMain() {
    struct udev *udev = udev_new();
    if (!udev) {
      TYPR_IO_LOG_ERROR("Listener (Linux/libinput): udev_new() failed");
      running.store(false);
      ready.store(false);
      return;
    }

    li = libinput_udev_create_context(&kInterface, nullptr, udev);
    if (!li) {
      TYPR_IO_LOG_ERROR(
          "Listener (Linux/libinput): libinput_udev_create_context() failed");
      udev_unref(udev);
      running.store(false);
      ready.store(false);
      return;
    }

    if (libinput_udev_assign_seat(li, "seat0") < 0) {
      TYPR_IO_LOG_ERROR(
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
      TYPR_IO_LOG_ERROR("Listener (Linux/libinput): xkb_context_new() failed");
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
    std::string rulesStr, modelStr, layoutStr, variantStr, optionsStr;

    // First consult common environment variables that various
    // systems/compositors set for XKB.
    if (const char *env = std::getenv("XKB_DEFAULT_RULES"))
      rulesStr = env;
    if (const char *env = std::getenv("XKB_DEFAULT_MODEL"))
      modelStr = env;
    if (const char *env = std::getenv("XKB_DEFAULT_LAYOUT"))
      layoutStr = env;
    if (const char *env = std::getenv("XKB_DEFAULT_VARIANT"))
      variantStr = env;
    if (const char *env = std::getenv("XKB_DEFAULT_OPTIONS"))
      optionsStr = env;

    // Fallback: parse Debian/Ubuntu-style /etc/default/keyboard if some pieces
    // of information are missing. This file often contains the active layout
    // information for local consoles and can supplement the environment.
    if (rulesStr.empty() || modelStr.empty() || layoutStr.empty() ||
        variantStr.empty() || optionsStr.empty()) {
      std::ifstream f("/etc/default/keyboard");
      if (f) {
        std::string line;
        while (std::getline(f, line)) {
          // Strip comments and surrounding whitespace.
          size_t comment = line.find('#');
          if (comment != std::string::npos)
            line = line.substr(0, comment);
          auto trim = [](std::string &s) {
            const char *ws = " \t\r\n";
            size_t a = s.find_first_not_of(ws);
            if (a == std::string::npos) {
              s.clear();
              return;
            }
            size_t b = s.find_last_not_of(ws);
            s = s.substr(a, b - a + 1);
          };
          trim(line);
          if (line.empty())
            continue;
          size_t eq = line.find('=');
          if (eq == std::string::npos)
            continue;
          std::string key = line.substr(0, eq);
          std::string val = line.substr(eq + 1);
          trim(key);
          trim(val);
          // Remove surrounding quotes if present.
          if (val.size() >= 2 && ((val.front() == '\"' && val.back() == '\"') ||
                                  (val.front() == '\'' && val.back() == '\'')))
            val = val.substr(1, val.size() - 2);
          // Normalize key for comparison.
          std::transform(key.begin(), key.end(), key.begin(),
                         [](unsigned char c) { return std::toupper(c); });
          if (key == "XKBRULES" || key == "XKB_DEFAULT_RULES")
            rulesStr = val;
          else if (key == "XKBMODEL" || key == "XKB_DEFAULT_MODEL")
            modelStr = val;
          else if (key == "XKBLAYOUT" || key == "XKB_DEFAULT_LAYOUT")
            layoutStr = val;
          else if (key == "XKBVARIANT" || key == "XKB_DEFAULT_VARIANT")
            variantStr = val;
          else if (key == "XKBOPTIONS" || key == "XKB_DEFAULT_OPTIONS")
            optionsStr = val;
        }
      }
    }
    // If layout still missing, try to heuristically guess it from locale
    // environment variables (LC_ALL, LC_MESSAGES, LANG). This provides a
    // reasonable fallback on systems where XKB defaults are not available.
    if (layoutStr.empty()) {
      const char *localeEnv = std::getenv("LC_ALL");
      if (!localeEnv)
        localeEnv = std::getenv("LC_MESSAGES");
      if (!localeEnv)
        localeEnv = std::getenv("LANG");
      if (localeEnv) {
        std::string locale(localeEnv);
        // Trim off encoding/variants (e.g. en_US.UTF-8 -> en_US)
        size_t dot = locale.find('.');
        if (dot != std::string::npos)
          locale.resize(dot);
        size_t at = locale.find('@');
        if (at != std::string::npos)
          locale.resize(at);
        // Split language and region if present (e.g. en_US)
        std::string lang = locale;
        std::string region;
        size_t us = locale.find('_');
        if (us != std::string::npos) {
          lang = locale.substr(0, us);
          region = locale.substr(us + 1);
        }
        std::transform(lang.begin(), lang.end(), lang.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::transform(region.begin(), region.end(), region.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        // Common heuristics:
        if (lang == "en") {
          // Prefer GB for en_GB, otherwise default to US
          if (region == "GB" || region == "UK")
            layoutStr = "gb";
          else
            layoutStr = "us";
        } else if (lang == "pt" && region == "BR") {
          layoutStr = "br";
        } else if (lang == "da") {
          layoutStr = "dk"; // Danish xkb uses 'dk'
        } else if (lang == "sv") {
          layoutStr = "se"; // Swedish xkb uses 'se'
        } else if (!lang.empty()) {
          // Fall back to using the language code as layout (fr, de, es, it,
          // etc.)
          layoutStr = lang;
        }
        TYPR_IO_LOG_DEBUG("Listener (Linux/libinput): guessed layout from "
                          "locale: %s",
                          layoutStr.c_str());
      }
    }

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
      TYPR_IO_LOG_DEBUG("Listener (Linux/libinput): xkb names: %s",
                        dbg.c_str());
    }

    xkbKeymap =
        xkb_keymap_new_from_names(xkbCtx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkbKeymap) {
      TYPR_IO_LOG_ERROR(
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
      TYPR_IO_LOG_ERROR("Listener (Linux/libinput): xkb_state_new() failed");
      xkb_keymap_unref(xkbKeymap);
      xkb_context_unref(xkbCtx);
      libinput_unref(li);
      udev_unref(udev);
      running.store(false);
      ready.store(false);
      return;
    }

    ready.store(true);
    TYPR_IO_LOG_INFO("Listener (Linux/libinput): Monitoring started");

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
      // Compute and stash the codepoint for this key so that it can be
      // delivered on key-release events (many applications deliver the
      // resulting character on release).
      codepoint = static_cast<char32_t>(xkb_keysym_to_utf32(sym));
      pendingCodepoints[keycode] = codepoint;
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
      TYPR_IO_LOG_DEBUG("Listener (Linux/libinput) %s: evdev=%u keysym=%u "
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
  TYPR_IO_LOG_DEBUG("Listener::start() called (Linux/libinput)");
  return m_impl ? m_impl->start(std::move(cb)) : false;
}

void Listener::stop() {
  TYPR_IO_LOG_DEBUG("Listener::stop() called (Linux/libinput)");
  if (m_impl)
    m_impl->stop();
}

bool Listener::isListening() const {
  TYPR_IO_LOG_DEBUG("Listener::isListening() called (Linux/libinput)");
  return m_impl ? m_impl->isRunning() : false;
}

} // namespace typr::io

#endif // __linux__
