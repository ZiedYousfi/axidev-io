/**
 * @file c_api.cpp
 * @brief C API implementation for typr-io keyboard functionality.
 *
 * Implements the C-compatible wrapper declared in `<typr-io/c_api.h>`.
 * Wraps `typr::io::keyboard::Sender` and `typr::io::keyboard::Listener`
 * as opaque C handles.
 *
 * The implementation is intentionally defensive: C++ exceptions are caught
 * and converted into a process-wide last-error string retrievable via
 * `typr_io_get_last_error`.
 *
 * @note Listener callbacks may be invoked from background threads; the wrapper
 *       safely bridges those events into C callbacks while protecting callback
 *       state with a mutex.
 */

#include <typr-io/c_api.h>

#include <cstdlib>
#include <cstring>

#include <mutex>
#include <new>
#include <string>

#include <typr-io/core.hpp>
#include <typr-io/keyboard/common.hpp>
#include <typr-io/keyboard/listener.hpp>
#include <typr-io/keyboard/sender.hpp>

namespace {

/**
 * @brief Internal wrapper that owns a typr::io::keyboard::Sender instance.
 *
 * SenderWrapper instances are heap-allocated and returned to C callers as
 * opaque handles (`typr_io_keyboard_sender_t`). They encapsulate the C++
 * `Sender` object used by the C API implementation.
 */
struct SenderWrapper {
  typr::io::keyboard::Sender sender;
};

/**
 * @brief Internal wrapper that contains a Listener and its C callback state.
 *
 * The `cb` and `user_data` fields are protected by `cb_mutex` so that both
 * the C API entry points and the listener's internal event threads can safely
 * access and update them.
 */
struct ListenerWrapper {
  typr::io::keyboard::Listener listener;
  typr_io_keyboard_listener_cb cb{nullptr};
  void *user_data{nullptr};
  std::mutex cb_mutex; // protects cb & user_data
};

/**
 * @brief Process-global last-error storage used by the C API implementation.
 *
 * The last error is protected by a mutex so it can be safely set and read
 * from multiple threads. Callers can retrieve a heap-allocated copy via
 * `typr_io_get_last_error`.
 */
static std::mutex g_last_error_mutex;
static std::string g_last_error;

/**
 * @brief Set the process-wide last error message (thread-safe).
 * @param s The error message to record (copied).
 */
static void set_last_error(const std::string &s) {
  std::lock_guard<std::mutex> lk(g_last_error_mutex);
  g_last_error = s;
}

/**
 * @brief Clear the process-wide last error message (thread-safe).
 */
static void clear_last_error() {
  std::lock_guard<std::mutex> lk(g_last_error_mutex);
  g_last_error.clear();
}

/**
 * @brief Duplicate a std::string into a C-allocated null-terminated buffer.
 *
 * The returned buffer must be freed by the caller (for example via
 * `typr_io_free_string` or `std::free`). Returns nullptr on allocation
 * failure.
 *
 * @param s Source string to duplicate.
 * @return char* Heap-allocated null-terminated copy or nullptr on OOM.
 */
static char *duplicate_c_string(const std::string &s) {
  size_t n = s.size();
  char *p = static_cast<char *>(std::malloc(n + 1));
  if (!p) {
    return nullptr;
  }
  std::memcpy(p, s.data(), n);
  p[n] = '\0';
  return p;
}

} // namespace

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Sender implementation ---------------- */

TYPR_IO_API typr_io_keyboard_sender_t typr_io_keyboard_sender_create(void) {
  try {
    clear_last_error();
    SenderWrapper *w = new (std::nothrow) SenderWrapper();
    if (!w) {
      set_last_error("Out of memory (sender)");
      return nullptr;
    }
    return reinterpret_cast<typr_io_keyboard_sender_t>(w);
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return nullptr;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_create");
    return nullptr;
  }
}

TYPR_IO_API void
typr_io_keyboard_sender_destroy(typr_io_keyboard_sender_t sender) {
  if (!sender) {
    return;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    delete w;
  } catch (const std::exception &e) {
    set_last_error(e.what());
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_destroy");
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_is_ready(typr_io_keyboard_sender_t sender) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.isReady();
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_is_ready");
    return false;
  }
}

TYPR_IO_API uint8_t
typr_io_keyboard_sender_type(typr_io_keyboard_sender_t sender) {
  if (!sender) {
    set_last_error("sender is NULL");
    return static_cast<uint8_t>(typr::io::keyboard::BackendType::Unknown);
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return static_cast<uint8_t>(w->sender.type());
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return static_cast<uint8_t>(typr::io::keyboard::BackendType::Unknown);
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_type");
    return static_cast<uint8_t>(typr::io::keyboard::BackendType::Unknown);
  }
}

TYPR_IO_API void typr_io_keyboard_sender_get_capabilities(
    typr_io_keyboard_sender_t sender,
    typr_io_keyboard_capabilities_t *out_capabilities) {
  if (!out_capabilities) {
    set_last_error("out_capabilities is NULL");
    return;
  }
  if (!sender) {
    set_last_error("sender is NULL");
    // Leave out_capabilities untouched or zero-initialized; we'll zero to be
    // safe.
    std::memset(out_capabilities, 0, sizeof(*out_capabilities));
    return;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    typr::io::keyboard::Capabilities caps = w->sender.capabilities();
    out_capabilities->can_inject_keys = caps.canInjectKeys;
    out_capabilities->can_inject_text = caps.canInjectText;
    out_capabilities->can_simulate_hid = caps.canSimulateHID;
    out_capabilities->supports_key_repeat = caps.supportsKeyRepeat;
    out_capabilities->needs_accessibility_perm = caps.needsAccessibilityPerm;
    out_capabilities->needs_input_monitoring_perm =
        caps.needsInputMonitoringPerm;
    out_capabilities->needs_uinput_access = caps.needsUinputAccess;
  } catch (const std::exception &e) {
    set_last_error(e.what());
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_get_capabilities");
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_request_permissions(typr_io_keyboard_sender_t sender) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.requestPermissions();
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_request_permissions");
    return false;
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_key_down(typr_io_keyboard_sender_t sender,
                                 typr_io_keyboard_key_t key) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.keyDown(static_cast<typr::io::keyboard::Key>(key));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_key_down");
    return false;
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_key_up(typr_io_keyboard_sender_t sender,
                               typr_io_keyboard_key_t key) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.keyUp(static_cast<typr::io::keyboard::Key>(key));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_key_up");
    return false;
  }
}

TYPR_IO_API bool typr_io_keyboard_sender_tap(typr_io_keyboard_sender_t sender,
                                             typr_io_keyboard_key_t key) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.tap(static_cast<typr::io::keyboard::Key>(key));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_tap");
    return false;
  }
}

TYPR_IO_API typr_io_keyboard_modifier_t
typr_io_keyboard_sender_active_modifiers(typr_io_keyboard_sender_t sender) {
  if (!sender) {
    set_last_error("sender is NULL");
    return 0;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    typr::io::keyboard::Modifier m = w->sender.activeModifiers();
    return static_cast<typr_io_keyboard_modifier_t>(static_cast<uint8_t>(m));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return 0;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_active_modifiers");
    return 0;
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_hold_modifier(typr_io_keyboard_sender_t sender,
                                      typr_io_keyboard_modifier_t mods) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.holdModifier(
        static_cast<typr::io::keyboard::Modifier>(mods));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_hold_modifier");
    return false;
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_release_modifier(typr_io_keyboard_sender_t sender,
                                         typr_io_keyboard_modifier_t mods) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.releaseModifier(
        static_cast<typr::io::keyboard::Modifier>(mods));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_release_modifier");
    return false;
  }
}

TYPR_IO_API bool typr_io_keyboard_sender_release_all_modifiers(
    typr_io_keyboard_sender_t sender) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.releaseAllModifiers();
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_release_all_modifiers");
    return false;
  }
}

TYPR_IO_API bool typr_io_keyboard_sender_combo(typr_io_keyboard_sender_t sender,
                                               typr_io_keyboard_modifier_t mods,
                                               typr_io_keyboard_key_t key) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.combo(static_cast<typr::io::keyboard::Modifier>(mods),
                           static_cast<typr::io::keyboard::Key>(key));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_combo");
    return false;
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_type_text_utf8(typr_io_keyboard_sender_t sender,
                                       const char *utf8_text) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  if (!utf8_text) {
    set_last_error("utf8_text is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.typeText(std::string(utf8_text));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_type_text_utf8");
    return false;
  }
}

TYPR_IO_API bool
typr_io_keyboard_sender_type_character(typr_io_keyboard_sender_t sender,
                                       uint32_t codepoint) {
  if (!sender) {
    set_last_error("sender is NULL");
    return false;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    return w->sender.typeCharacter(static_cast<char32_t>(codepoint));
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_type_character");
    return false;
  }
}

TYPR_IO_API void
typr_io_keyboard_sender_flush(typr_io_keyboard_sender_t sender) {
  if (!sender) {
    set_last_error("sender is NULL");
    return;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    w->sender.flush();
  } catch (const std::exception &e) {
    set_last_error(e.what());
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_sender_flush");
  }
}

TYPR_IO_API void
typr_io_keyboard_sender_set_key_delay(typr_io_keyboard_sender_t sender,
                                      uint32_t delay_us) {
  if (!sender) {
    set_last_error("sender is NULL");
    return;
  }
  try {
    clear_last_error();
    SenderWrapper *w = reinterpret_cast<SenderWrapper *>(sender);
    w->sender.setKeyDelay(delay_us);
  } catch (const std::exception &e) {
    set_last_error(e.what());
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_sender_set_key_delay");
  }
}

/* ---------------- Listener implementation ---------------- */

TYPR_IO_API typr_io_keyboard_listener_t typr_io_keyboard_listener_create(void) {
  try {
    clear_last_error();
    ListenerWrapper *w = new (std::nothrow) ListenerWrapper();
    if (!w) {
      set_last_error("Out of memory (listener)");
      return nullptr;
    }
    return reinterpret_cast<typr_io_keyboard_listener_t>(w);
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return nullptr;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_listener_create");
    return nullptr;
  }
}

TYPR_IO_API void
typr_io_keyboard_listener_destroy(typr_io_keyboard_listener_t listener) {
  if (!listener) {
    return;
  }
  try {
    clear_last_error();
    ListenerWrapper *w = reinterpret_cast<ListenerWrapper *>(listener);
    // Ensure the listener is stopped before destroying to avoid races with
    // callbacks originating from background threads.
    try {
      w->listener.stop();
    } catch (...) {
      // Ignore stop failures; we still proceed to delete the wrapper.
    }
    delete w;
  } catch (const std::exception &e) {
    set_last_error(e.what());
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_listener_destroy");
  }
}

TYPR_IO_API bool
typr_io_keyboard_listener_start(typr_io_keyboard_listener_t listener,
                                typr_io_keyboard_listener_cb cb,
                                void *user_data) {
  if (!listener) {
    set_last_error("listener is NULL");
    return false;
  }
  if (!cb) {
    set_last_error("callback is NULL");
    return false;
  }
  try {
    clear_last_error();
    ListenerWrapper *w = reinterpret_cast<ListenerWrapper *>(listener);

    // Store callback and user_data before starting to avoid a race where an
    // internal thread invokes the callback immediately after start() returns.
    {
      std::lock_guard<std::mutex> lk(w->cb_mutex);
      w->cb = cb;
      w->user_data = user_data;
    }

    /**
     * @internal
     * @brief Bridge that forwards `typr::io::keyboard::Listener` events to the
     * C callback.
     *
     * Responsibilities:
     *  - Acquire `w->cb_mutex` to safely copy the stored C callback pointer
     *    and the associated `user_data` pointer so invocation can occur
     *    without holding the lock.
     *  - Convert/normalize types for the C ABI: `char32_t` -> `uint32_t`,
     *    `typr::io::keyboard::Key` -> `typr_io_keyboard_key_t`,
     * `typr::io::keyboard::Modifier` -> `typr_io_modifier_t`.
     *  - Invoke the user-supplied C callback if present. Any exceptions thrown
     *    by the C callback are caught and swallowed to prevent exceptions from
     *    escaping into the C++ internals (unwinding across the C ABI is
     * undefined).
     *
     * Notes:
     *  - This lambda is invoked on the listener's internal thread. The C
     *    callback must be thread-safe and avoid long/blocking operations.
     */
    auto bridge = [w](char32_t codepoint, typr::io::keyboard::Key key,
                      typr::io::keyboard::Modifier mods, bool pressed) {
      // Forward event to C callback; protect access to cb & user_data.
      typr_io_keyboard_listener_cb local_cb = nullptr;
      void *local_ud = nullptr;
      {
        std::lock_guard<std::mutex> lk(w->cb_mutex);
        local_cb = w->cb;
        local_ud = w->user_data;
      }
      if (local_cb) {
        try {
          local_cb(static_cast<uint32_t>(codepoint),
                   static_cast<typr_io_keyboard_key_t>(key),
                   static_cast<typr_io_keyboard_modifier_t>(
                       static_cast<uint8_t>(mods)),
                   pressed, local_ud);
        } catch (...) {
          // Swallow exceptions from user-provided C callbacks to avoid letting
          // them unwind into C++ internals.
        }
      }
    };

    bool ok = w->listener.start(bridge);
    if (!ok) {
      // On failure clear stored callback so listener_destroy/stop sees a clean
      // state.
      std::lock_guard<std::mutex> lk(w->cb_mutex);
      w->cb = nullptr;
      w->user_data = nullptr;
    }
    return ok;
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_listener_start");
    return false;
  }
}

TYPR_IO_API void
typr_io_keyboard_listener_stop(typr_io_keyboard_listener_t listener) {
  if (!listener) {
    set_last_error("listener is NULL");
    return;
  }
  try {
    clear_last_error();
    ListenerWrapper *w = reinterpret_cast<ListenerWrapper *>(listener);
    w->listener.stop();
    // Clear callback & user_data so subsequent events are ignored.
    std::lock_guard<std::mutex> lk(w->cb_mutex);
    w->cb = nullptr;
    w->user_data = nullptr;
  } catch (const std::exception &e) {
    set_last_error(e.what());
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_listener_stop");
  }
}

TYPR_IO_API bool
typr_io_keyboard_listener_is_listening(typr_io_keyboard_listener_t listener) {
  if (!listener) {
    set_last_error("listener is NULL");
    return false;
  }
  try {
    clear_last_error();
    ListenerWrapper *w = reinterpret_cast<ListenerWrapper *>(listener);
    return w->listener.isListening();
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return false;
  } catch (...) {
    set_last_error(
        "Unknown exception in typr_io_keyboard_listener_is_listening");
    return false;
  }
}

/* ---------------- Utilities ---------------- */

TYPR_IO_API char *typr_io_keyboard_key_to_string(typr_io_keyboard_key_t key) {
  try {
    clear_last_error();
    std::string s = typr::io::keyboard::keyToString(
        static_cast<typr::io::keyboard::Key>(key));
    return duplicate_c_string(s);
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return nullptr;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_key_to_string");
    return nullptr;
  }
}

TYPR_IO_API typr_io_keyboard_key_t
typr_io_keyboard_string_to_key(const char *name) {
  if (!name) {
    set_last_error("name is NULL");
    return static_cast<typr_io_keyboard_key_t>(
        typr::io::keyboard::Key::Unknown);
  }
  try {
    clear_last_error();
    typr::io::keyboard::Key k =
        typr::io::keyboard::stringToKey(std::string(name));
    return static_cast<typr_io_keyboard_key_t>(k);
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return static_cast<typr_io_keyboard_key_t>(
        typr::io::keyboard::Key::Unknown);
  } catch (...) {
    set_last_error("Unknown exception in typr_io_keyboard_string_to_key");
    return static_cast<typr_io_keyboard_key_t>(
        typr::io::keyboard::Key::Unknown);
  }
}

TYPR_IO_API const char *typr_io_library_version(void) {
  try {
    clear_last_error();
    return typr::io::libraryVersion();
  } catch (const std::exception &e) {
    set_last_error(e.what());
    return nullptr;
  } catch (...) {
    set_last_error("Unknown exception in typr_io_library_version");
    return nullptr;
  }
}

TYPR_IO_API char *typr_io_get_last_error(void) {
  std::lock_guard<std::mutex> lk(g_last_error_mutex);
  if (g_last_error.empty()) {
    return nullptr;
  }
  return duplicate_c_string(g_last_error);
}

TYPR_IO_API void typr_io_clear_last_error(void) { clear_last_error(); }

TYPR_IO_API void typr_io_free_string(char *s) {
  if (!s) {
    return;
  }
  std::free(s);
}

#ifdef __cplusplus
} /* extern \"C\" */
#endif
