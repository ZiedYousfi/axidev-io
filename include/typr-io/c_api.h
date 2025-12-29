/**
 * @file c_api.h
 * @brief C-compatible wrapper for the typr-io keyboard functionality.
 *
 * This header provides a minimal, stable C ABI suitable for language bindings
 * and simple consumers that cannot directly link against the C++ API.
 *
 * The C API wraps the keyboard-specific `typr::io::keyboard::Sender` and
 * `typr::io::keyboard::Listener` classes, providing cross-platform keyboard
 * input injection and global keyboard event monitoring.
 *
 * Notes:
 *  - The C API is intentionally thin: it exposes opaque keyboard
 * sender/listener handles and a small set of helper functions to operate them.
 *  - Exported symbols are decorated with `TYPR_IO_API`. When included from
 *    C++ this macro is reused from <typr-io/core.hpp>. When included from C a
 *    safe no-op fallback is provided below.
 *
 * Threading / callbacks:
 *  - Keyboard listener callbacks may be invoked on an internal background
 * thread. The provided callback must be thread-safe and avoid long-blocking
 * work.
 *
 * Memory ownership:
 *  - Functions that return strings allocate heap memory which callers must
 *    free via `typr_io_free_string`.
 *
 * Example:
 * @code{.c}
 * #include <typr-io/c_api.h>
 *
 * int main(void) {
 *   typr_io_keyboard_sender_t s = typr_io_keyboard_sender_create();
 *   if (!s) return 1;
 *
 *   typr_io_keyboard_capabilities_t caps;
 *   typr_io_keyboard_sender_get_capabilities(s, &caps);
 *   if (caps.can_inject_keys) {
 *     typr_io_keyboard_sender_tap(s, typr_io_keyboard_string_to_key("A"));
 *   }
 *
 *   typr_io_keyboard_sender_destroy(s);
 *   return 0;
 * }
 * @endcode
 */

#pragma once
#ifndef TYPR_IO_C_API_H
#define TYPR_IO_C_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Ensure the export macro `TYPR_IO_API` exists.
 *
 * Consumers building or using the C++ library will normally obtain a
 * definition for `TYPR_IO_API` (for example to apply visibility attributes).
 * When this header is included in a plain C translation unit the macro may be
 * undefined; provide a safe no-op fallback in that case.
 */
#ifndef TYPR_IO_API
#define TYPR_IO_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle types for keyboard input/output operations.
 *
 * These are opaque pointers to library-managed C++ objects that wrap
 * `typr::io::keyboard::Sender` and `typr::io::keyboard::Listener`.
 * Clients must treat them as opaque handles and use the provided
 * create/destroy functions to manage their lifetime.
 *
 * - `typr_io_keyboard_sender_t`: Keyboard input injection (wraps
 * keyboard::Sender)
 * - `typr_io_keyboard_listener_t`: Global keyboard event monitoring (wraps
 * keyboard::Listener)
 */
typedef void *typr_io_keyboard_sender_t;
typedef void *typr_io_keyboard_listener_t;

/**
 * @brief Primitive types used for keys and modifiers in the C API.
 *
 * These types match the layout used by the C++ API: `typr_io_keyboard_key_t`
 * corresponds to `typr::io::Key`, and `typr_io_modifier_t` is a bitmask that
 * corresponds to `typr::io::Modifier`.
 */
typedef uint16_t typr_io_keyboard_key_t; /* corresponds to typr::io::Key */
typedef uint8_t typr_io_keyboard_modifier_t; /* bitmask, corresponds to
                                                typr::io::Modifier */

/**
 * @brief Common modifier bit masks for `typr_io_modifier_t`.
 *
 * These flags can be tested or combined when manipulating modifier state via
 * the C API.
 */
enum {
  TYPR_IO_MOD_SHIFT = 0x01,
  TYPR_IO_MOD_CTRL = 0x02,
  TYPR_IO_MOD_ALT = 0x04,
  TYPR_IO_MOD_SUPER = 0x08,
  TYPR_IO_MOD_CAPSLOCK = 0x10,
  TYPR_IO_MOD_NUMLOCK = 0x20,
};

/**
 * @brief Backend capabilities description (mirrors typr::io::Capabilities).
 *
 * Members indicate features supported or requirements imposed by the active
 * backend implementation.
 *
 * @var typr_io_keyboard_capabilities_t::can_inject_keys True if the backend can send
 * physical key events.
 * @var typr_io_keyboard_capabilities_t::can_inject_text True if the backend can inject
 * arbitrary Unicode text.
 * @var typr_io_keyboard_capabilities_t::can_simulate_hid True if the backend simulates
 * low-level HID events (e.g., uinput).
 * @var typr_io_keyboard_capabilities_t::supports_key_repeat True if key repeat is
 * supported by the backend.
 * @var typr_io_keyboard_capabilities_t::needs_accessibility_perm True if accessibility
 * permission is required (platform-dependent).
 * @var typr_io_keyboard_capabilities_t::needs_input_monitoring_perm True if input
 * monitoring permission is required (platform-dependent).
 * @var typr_io_keyboard_capabilities_t::needs_uinput_access True if uinput or similar
 * device access is required.
 */
typedef struct typr_io_keyboard_capabilities_t {
  bool can_inject_keys;
  bool can_inject_text;
  bool can_simulate_hid;
  bool supports_key_repeat;
  bool needs_accessibility_perm;
  bool needs_input_monitoring_perm;
  bool needs_uinput_access;
} typr_io_keyboard_capabilities_t;

/**
 * @typedef typr_io_keyboard_listener_cb
 * @brief Keyboard listener callback invoked for each observed key event.
 *
 * This callback is invoked by the keyboard listener for each global keyboard
 * event detected on the system.
 *
 * @param codepoint Unicode codepoint produced by the key event (0 if none).
 * @param key Logical key id (typr_io_keyboard_key_t; 0 if unknown).
 * @param mods Current modifier bitmask (typr_io_modifier_t).
 * @param pressed True for key press, false for key release.
 * @param user_data Opaque pointer provided by the caller to
 * `typr_io_keyboard_listener_start`.
 */
typedef void (*typr_io_keyboard_listener_cb)(uint32_t codepoint,
                                             typr_io_keyboard_key_t key,
                                             typr_io_keyboard_modifier_t mods,
                                             bool pressed, void *user_data);

/** @name Keyboard Sender (keyboard input injection)
 * @brief Functions to create and operate a keyboard Sender for injecting
 * keyboard input.
 *
 * The keyboard sender provides cross-platform keyboard input injection,
 * supporting physical key events and Unicode text injection where available.
 * @{
 */

/**
 * @brief Create a new keyboard Sender instance.
 * @return typr_io_keyboard_sender_t Opaque keyboard sender handle, or NULL on
 * allocation failure.
 */
TYPR_IO_API typr_io_keyboard_sender_t typr_io_keyboard_sender_create(void);

/**
 * @brief Destroy a keyboard Sender instance.
 * @param sender Keyboard sender handle to destroy (safe to call with NULL).
 */
TYPR_IO_API void
typr_io_keyboard_sender_destroy(typr_io_keyboard_sender_t sender);

/**
 * @brief Check whether the sender's backend is ready to inject events.
 * @param sender Sender handle.
 * @return true If the backend is ready; false otherwise.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_is_ready(typr_io_keyboard_sender_t sender);

/**
 * @brief Get the active backend type used by the sender.
 * @param sender Sender handle.
 * @return uint8_t BackendType as an integer value (see typr::io::BackendType in
 * the C++ API).
 */
TYPR_IO_API uint8_t typr_io_keyboard_sender_type(
    typr_io_keyboard_sender_t sender); /* BackendType as integer */

/**
 * @brief Retrieve the backend capabilities.
 * @param sender Sender handle.
 * @param out_capabilities Output pointer that will be populated with
 * capabilities (must not be NULL).
 */
TYPR_IO_API void typr_io_keyboard_sender_get_capabilities(
    typr_io_keyboard_sender_t sender,
    typr_io_keyboard_capabilities_t *out_capabilities);

/**
 * @brief Request runtime permissions required by the backend
 * (platform-specific).
 * @param sender Sender handle.
 * @return true if the backend is ready after requesting permissions; false
 * otherwise.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_request_permissions(typr_io_keyboard_sender_t sender);

/**
 * @brief Simulate a physical key press.
 * @param sender Sender handle.
 * @param key Logical key to press.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_key_down(typr_io_keyboard_sender_t sender,
                                 typr_io_keyboard_key_t key);

/**
 * @brief Simulate a physical key release.
 * @param sender Sender handle.
 * @param key Logical key to release.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_key_up(typr_io_keyboard_sender_t sender,
                               typr_io_keyboard_key_t key);

/**
 * @brief Convenience: tap a key (press then release).
 * @param sender Sender handle.
 * @param key Logical key to tap.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool typr_io_keyboard_sender_tap(typr_io_keyboard_sender_t sender,
                                             typr_io_keyboard_key_t key);

/**
 * @brief Get the currently active modifiers.
 * @param sender Sender handle.
 * @return typr_io_modifier_t Current modifier bitmask.
 */
TYPR_IO_API typr_io_keyboard_modifier_t
typr_io_keyboard_sender_active_modifiers(typr_io_keyboard_sender_t sender);

/**
 * @brief Hold (press) the requested modifier keys.
 * @param sender Sender handle.
 * @param mods Modifier bitmask specifying which modifiers to hold.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_hold_modifier(typr_io_keyboard_sender_t sender,
                                      typr_io_keyboard_modifier_t mods);

/**
 * @brief Release the requested modifier keys.
 * @param sender Sender handle.
 * @param mods Modifier bitmask specifying which modifiers to release.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_release_modifier(typr_io_keyboard_sender_t sender,
                                         typr_io_keyboard_modifier_t mods);

/**
 * @brief Release all modifiers currently held by the sender.
 * @param sender Sender handle.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_release_all_modifiers(typr_io_keyboard_sender_t sender);

/**
 * @brief Execute a key combo: press modifiers, tap key, release modifiers.
 * @param sender Sender handle.
 * @param mods Modifier bitmask to hold during the combo.
 * @param key Key to tap while modifiers are held.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool typr_io_keyboard_sender_combo(typr_io_keyboard_sender_t sender,
                                               typr_io_keyboard_modifier_t mods,
                                               typr_io_keyboard_key_t key);

/**
 * @brief Inject UTF-8 text directly (layout-independent on supporting
 * backends).
 * @param sender Sender handle.
 * @param utf8_text Null-terminated UTF-8 string to inject.
 * @return true on success; false if not supported or on failure.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_type_text_utf8(typr_io_keyboard_sender_t sender,
                                       const char *utf8_text);

/**
 * @brief Inject a single Unicode codepoint.
 * @param sender Sender handle.
 * @param codepoint Unicode codepoint to inject.
 * @return true on success; false on failure.
 */
TYPR_IO_API bool
typr_io_keyboard_sender_type_character(typr_io_keyboard_sender_t sender,
                                       uint32_t codepoint);

/**
 * @brief Flush pending events (force delivery).
 * @param sender Sender handle.
 */
TYPR_IO_API void
typr_io_keyboard_sender_flush(typr_io_keyboard_sender_t sender);

/**
 * @brief Set the delay (in microseconds) used by convenience operations like
 * tap/combo.
 * @param sender Sender handle.
 * @param delay_us Delay in microseconds.
 */
TYPR_IO_API void
typr_io_keyboard_sender_set_key_delay(typr_io_keyboard_sender_t sender,
                                      uint32_t delay_us);
/** @} */ /* end of Keyboard Sender group */

/** @name Keyboard Listener (global keyboard event monitoring)
 * @brief Functions to create and control a global keyboard event listener.
 *
 * The keyboard listener monitors system-wide keyboard events and invokes
 * a user-provided callback for each key press and release.
 * @{
 */

/**
 * @brief Create a keyboard Listener instance.
 * @return typr_io_keyboard_listener_t Opaque keyboard listener handle, or NULL
 * on allocation failure.
 */
TYPR_IO_API typr_io_keyboard_listener_t typr_io_keyboard_listener_create(void);

/**
 * @brief Destroy a Listener instance.
 * @param listener Listener handle to destroy (safe to call with NULL).
 */
TYPR_IO_API void
typr_io_keyboard_listener_destroy(typr_io_keyboard_listener_t listener);

/**
 * @brief Start the listener. The callback may be invoked from an internal
 * thread.
 * @param listener Listener handle.
 * @param cb Callback invoked for each observed event.
 * @param user_data Opaque pointer forwarded to the callback.
 * @return true on success; false if the listener could not be started
 *         (for example due to missing permissions or platform support).
 */
TYPR_IO_API bool
typr_io_keyboard_listener_start(typr_io_keyboard_listener_t listener,
                                typr_io_keyboard_listener_cb cb,
                                void *user_data);

/**
 * @brief Stop the listener. Safe to call from any thread; a no-op if not
 * running.
 * @param listener Listener handle.
 */
TYPR_IO_API void
typr_io_keyboard_listener_stop(typr_io_keyboard_listener_t listener);

/**
 * @brief Query whether the listener is currently active.
 * @param listener Listener handle.
 * @return true if listening; false otherwise.
 */
TYPR_IO_API bool
typr_io_keyboard_listener_is_listening(typr_io_keyboard_listener_t listener);
/** @} */ /* end of Listener group */

/* ---------------- Utilities / Conversions ---------------- */

/**
 * @brief Convert a Key to a heap-allocated, null-terminated string.
 * @param key Key to convert.
 * @return char* Heap-allocated canonical name for the key (caller must free
 * with `typr_io_free_string`), or NULL on allocation failure.
 */
TYPR_IO_API char *typr_io_keyboard_key_to_string(typr_io_keyboard_key_t key);

/**
 * @brief Parse a textual key name to a `typr_io_keyboard_key_t` value.
 * @param name Null-terminated string (case-insensitive; accepts common aliases
 * like "esc", "space").
 * @return typr_io_keyboard_key_t Parsed key value, or 0 (Key::Unknown) for
 * unknown/invalid inputs.
 */
TYPR_IO_API typr_io_keyboard_key_t typr_io_keyboard_string_to_key(const char *name);

/**
 * @brief Get the library version string.
 * @return const char* Pointer to an internal, null-terminated version string
 * (do not free).
 */
TYPR_IO_API const char *typr_io_library_version(void);

/**
 * @brief Retrieve the last process-wide error string, if any.
 *
 * Many functions record a process-global "last error" string on failure.
 * The returned string is heap-allocated and must be freed with
 * `typr_io_free_string`. Returns NULL if there is no last error.
 *
 * @return char* Heap-allocated string with the last error message, or NULL.
 */
TYPR_IO_API char *typr_io_get_last_error(void);

/**
 * @brief Clear the process-wide last error string, if any.
 */
TYPR_IO_API void typr_io_clear_last_error(void);

/**
 * @brief Free a string returned by the C API.
 * @param s Pointer obtained from functions like `typr_io_keyboard_key_to_string` or
 * `typr_io_get_last_error`. Safe to call with NULL.
 */
TYPR_IO_API void typr_io_free_string(char *s);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TYPR_IO_C_API_H */
