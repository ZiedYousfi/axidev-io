/**
 * @file c_api.h
 * @brief C-compatible wrapper for the axidev-io keyboard functionality.
 *
 * This header provides a minimal, stable C ABI suitable for language bindings
 * and simple consumers that cannot directly link against the C++ API.
 *
 * The C API wraps the keyboard-specific `axidev::io::keyboard::Sender` and
 * `axidev::io::keyboard::Listener` classes, providing cross-platform keyboard
 * input injection and global keyboard event monitoring.
 *
 * @note **API Design: KeyWithModifier is the consumer-facing type.**
 *
 * All sender and listener APIs use `axidev_io_keyboard_key_with_modifier_t` to
 * represent a key combined with its required modifiers. This struct is the
 * standard unit of communication between consumers and the library.
 *
 * The raw key type (`axidev_io_keyboard_key_t`) and modifier type
 * (`axidev_io_keyboard_modifier_t`) are internal convenience types. Consumers
 * should always use `axidev_io_keyboard_key_with_modifier_t` when interacting
 * with the API.
 *
 * Notes:
 *  - The C API is intentionally thin: it exposes opaque keyboard
 * sender/listener handles and a small set of helper functions to operate them.
 *  - Exported symbols are decorated with `AXIDEV_IO_API`. When included from
 *    C++ this macro is reused from <axidev-io/core.hpp>. When included from C a
 *    safe no-op fallback is provided below.
 *
 * Threading / callbacks:
 *  - Keyboard listener callbacks may be invoked on an internal background
 * thread. The provided callback must be thread-safe and avoid long-blocking
 * work.
 *
 * Memory ownership:
 *  - Functions that return strings allocate heap memory which callers must
 *    free via `axidev_io_free_string`.
 *
 * Example:
 * @code{.c}
 * #include <axidev-io/c_api.h>
 *
 * int main(void) {
 *   axidev_io_keyboard_sender_t s = axidev_io_keyboard_sender_create();
 *   if (!s) return 1;
 *
 *   axidev_io_keyboard_capabilities_t caps;
 *   axidev_io_keyboard_sender_get_capabilities(s, &caps);
 *   if (caps.can_inject_keys) {
 *     // Parse "A" to get key with no modifiers
 *     axidev_io_keyboard_key_with_modifier_t key_mod;
 *     axidev_io_keyboard_string_to_key_with_modifier("A", &key_mod);
 *     axidev_io_keyboard_sender_tap(s, key_mod);
 *
 *     // For Ctrl+C combo:
 *     axidev_io_keyboard_string_to_key_with_modifier("Ctrl+C", &key_mod);
 *     axidev_io_keyboard_sender_tap(s, key_mod);
 *   }
 *
 *   axidev_io_keyboard_sender_destroy(s);
 *   return 0;
 * }
 * @endcode
 */

#pragma once
#ifndef AXIDEV_IO_C_API_H
#define AXIDEV_IO_C_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Ensure the export macro `AXIDEV_IO_API` exists.
 *
 * Consumers building or using the C++ library will normally obtain a
 * definition for `AXIDEV_IO_API` (for example to apply visibility attributes).
 * When this header is included in a plain C translation unit the macro may be
 * undefined; provide a safe no-op fallback in that case.
 */
#ifndef AXIDEV_IO_API
#define AXIDEV_IO_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle types for keyboard input/output operations.
 *
 * These are opaque pointers to library-managed C++ objects that wrap
 * `axidev::io::keyboard::Sender` and `axidev::io::keyboard::Listener`.
 * Clients must treat them as opaque handles and use the provided
 * create/destroy functions to manage their lifetime.
 *
 * - `axidev_io_keyboard_sender_t`: Keyboard input injection (wraps
 * keyboard::Sender)
 * - `axidev_io_keyboard_listener_t`: Global keyboard event monitoring (wraps
 * keyboard::Listener)
 */
typedef void *axidev_io_keyboard_sender_t;
typedef void *axidev_io_keyboard_listener_t;

/**
 * @brief Primitive types used for keys and modifiers in the C API.
 *
 * These types match the layout used by the C++ API: `axidev_io_keyboard_key_t`
 * corresponds to `axidev::io::Key`, and `axidev_io_modifier_t` is a bitmask that
 * corresponds to `axidev::io::Modifier`.
 */
typedef uint16_t axidev_io_keyboard_key_t; /* corresponds to axidev::io::Key */
typedef uint8_t axidev_io_keyboard_modifier_t; /* bitmask, corresponds to
                                                axidev::io::Modifier */

/**
 * @struct axidev_io_keyboard_key_with_modifier_t
 * @brief Represents a logical key combined with its required modifiers.
 */
typedef struct axidev_io_keyboard_key_with_modifier_t {
  axidev_io_keyboard_key_t key;
  axidev_io_keyboard_modifier_t mods;
} axidev_io_keyboard_key_with_modifier_t;

/**
 * @brief Common modifier bit masks for `axidev_io_modifier_t`.
 *
 * These flags can be tested or combined when manipulating modifier state via
 * the C API.
 */
enum {
  AXIDEV_IO_MOD_SHIFT = 0x01,
  AXIDEV_IO_MOD_CTRL = 0x02,
  AXIDEV_IO_MOD_ALT = 0x04,
  AXIDEV_IO_MOD_SUPER = 0x08,
  AXIDEV_IO_MOD_CAPSLOCK = 0x10,
  AXIDEV_IO_MOD_NUMLOCK = 0x20,
};

/**
 * @brief Backend capabilities description (mirrors axidev::io::Capabilities).
 *
 * Members indicate features supported or requirements imposed by the active
 * backend implementation.
 *
 * @var axidev_io_keyboard_capabilities_t::can_inject_keys True if the backend can send
 * physical key events.
 * @var axidev_io_keyboard_capabilities_t::can_inject_text True if the backend can inject
 * arbitrary Unicode text.
 * @var axidev_io_keyboard_capabilities_t::can_simulate_hid True if the backend simulates
 * low-level HID events (e.g., uinput).
 * @var axidev_io_keyboard_capabilities_t::supports_key_repeat True if key repeat is
 * supported by the backend.
 * @var axidev_io_keyboard_capabilities_t::needs_accessibility_perm True if accessibility
 * permission is required (platform-dependent).
 * @var axidev_io_keyboard_capabilities_t::needs_input_monitoring_perm True if input
 * monitoring permission is required (platform-dependent).
 * @var axidev_io_keyboard_capabilities_t::needs_uinput_access True if uinput or similar
 * device access is required.
 */
typedef struct axidev_io_keyboard_capabilities_t {
  bool can_inject_keys;
  bool can_inject_text;
  bool can_simulate_hid;
  bool supports_key_repeat;
  bool needs_accessibility_perm;
  bool needs_input_monitoring_perm;
  bool needs_uinput_access;
} axidev_io_keyboard_capabilities_t;

/**
 * @typedef axidev_io_keyboard_listener_cb
 * @brief Keyboard listener callback invoked for each observed key event.
 *
 * This callback is invoked by the keyboard listener for each global keyboard
 * event detected on the system.
 *
 * @param codepoint Unicode codepoint produced by the key event (0 if none).
 *                  This value is provided for convenience but is often not
 *                  needed; usage of @p key_mod is generally preferred
 *                  for consistency and portability across backends.
 * @param key_mod Logical key and modifier state.
 * @param pressed True for key press, false for key release.
 * @param user_data Opaque pointer provided by the caller to
 * `axidev_io_keyboard_listener_start`.
 */
typedef void (*axidev_io_keyboard_listener_cb)(
    uint32_t codepoint, axidev_io_keyboard_key_with_modifier_t key_mod,
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
 * @return axidev_io_keyboard_sender_t Opaque keyboard sender handle, or NULL on
 * allocation failure.
 */
AXIDEV_IO_API axidev_io_keyboard_sender_t axidev_io_keyboard_sender_create(void);

/**
 * @brief Destroy a keyboard Sender instance.
 * @param sender Keyboard sender handle to destroy (safe to call with NULL).
 */
AXIDEV_IO_API void
axidev_io_keyboard_sender_destroy(axidev_io_keyboard_sender_t sender);

/**
 * @brief Check whether the sender's backend is ready to inject events.
 * @param sender Sender handle.
 * @return true If the backend is ready; false otherwise.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_is_ready(axidev_io_keyboard_sender_t sender);

/**
 * @brief Get the active backend type used by the sender.
 * @param sender Sender handle.
 * @return uint8_t BackendType as an integer value (see axidev::io::BackendType in
 * the C++ API).
 */
AXIDEV_IO_API uint8_t axidev_io_keyboard_sender_type(
    axidev_io_keyboard_sender_t sender); /* BackendType as integer */

/**
 * @brief Retrieve the backend capabilities.
 * @param sender Sender handle.
 * @param out_capabilities Output pointer that will be populated with
 * capabilities (must not be NULL).
 */
AXIDEV_IO_API void axidev_io_keyboard_sender_get_capabilities(
    axidev_io_keyboard_sender_t sender,
    axidev_io_keyboard_capabilities_t *out_capabilities);

/**
 * @brief Request runtime permissions required by the backend
 * (platform-specific).
 * @param sender Sender handle.
 * @return true if the backend is ready after requesting permissions; false
 * otherwise.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_request_permissions(axidev_io_keyboard_sender_t sender);

/**
 * @brief Simulate a physical key press with modifiers.
 *
 * The modifiers in `key_mod.mods` are pressed before the key.
 *
 * @param sender Sender handle.
 * @param key_mod Key with modifiers to press.
 * @return true on success; false on failure.
 */
AXIDEV_IO_API bool axidev_io_keyboard_sender_key_down(
    axidev_io_keyboard_sender_t sender,
    axidev_io_keyboard_key_with_modifier_t key_mod);

/**
 * @brief Simulate a physical key release with modifiers.
 *
 * Releases the key and any modifiers specified in `key_mod.mods`.
 *
 * @param sender Sender handle.
 * @param key_mod Key with modifiers to release.
 * @return true on success; false on failure.
 */
AXIDEV_IO_API bool axidev_io_keyboard_sender_key_up(
    axidev_io_keyboard_sender_t sender,
    axidev_io_keyboard_key_with_modifier_t key_mod);

/**
 * @brief Convenience: tap a key with its modifiers (press then release).
 *
 * This is the primary method for sending a key event. It handles:
 * 1. Pressing the required modifiers
 * 2. Pressing and releasing the key
 * 3. Releasing the modifiers
 *
 * @param sender Sender handle.
 * @param key_mod Key with modifiers to tap.
 * @return true on success; false on failure.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_tap(axidev_io_keyboard_sender_t sender,
                              axidev_io_keyboard_key_with_modifier_t key_mod);

/**
 * @brief Get the currently active modifiers.
 * @param sender Sender handle.
 * @return axidev_io_modifier_t Current modifier bitmask.
 */
AXIDEV_IO_API axidev_io_keyboard_modifier_t
axidev_io_keyboard_sender_active_modifiers(axidev_io_keyboard_sender_t sender);

/**
 * @brief Hold (press) the requested modifier keys.
 * @param sender Sender handle.
 * @param mods Modifier bitmask specifying which modifiers to hold.
 * @return true on success; false on failure.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_hold_modifier(axidev_io_keyboard_sender_t sender,
                                      axidev_io_keyboard_modifier_t mods);

/**
 * @brief Release the requested modifier keys.
 * @param sender Sender handle.
 * @param mods Modifier bitmask specifying which modifiers to release.
 * @return true on success; false on failure.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_release_modifier(axidev_io_keyboard_sender_t sender,
                                         axidev_io_keyboard_modifier_t mods);

/**
 * @brief Release all modifiers currently held by the sender.
 * @param sender Sender handle.
 * @return true on success; false on failure.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_release_all_modifiers(axidev_io_keyboard_sender_t sender);

/**
 * @brief Inject UTF-8 text directly (layout-independent on supporting
 * backends).
 * @param sender Sender handle.
 * @param utf8_text Null-terminated UTF-8 string to inject.
 * @return true on success; false if not supported or on failure.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_type_text_utf8(axidev_io_keyboard_sender_t sender,
                                       const char *utf8_text);

/**
 * @brief Inject a single Unicode codepoint.
 * @param sender Sender handle.
 * @param codepoint Unicode codepoint to inject.
 * @return true on success; false on failure.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_sender_type_character(axidev_io_keyboard_sender_t sender,
                                       uint32_t codepoint);

/**
 * @brief Flush pending events (force delivery).
 * @param sender Sender handle.
 */
AXIDEV_IO_API void
axidev_io_keyboard_sender_flush(axidev_io_keyboard_sender_t sender);

/**
 * @brief Set the delay (in microseconds) used by convenience operations like
 * tap/combo.
 * @param sender Sender handle.
 * @param delay_us Delay in microseconds.
 */
AXIDEV_IO_API void
axidev_io_keyboard_sender_set_key_delay(axidev_io_keyboard_sender_t sender,
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
 * @return axidev_io_keyboard_listener_t Opaque keyboard listener handle, or NULL
 * on allocation failure.
 */
AXIDEV_IO_API axidev_io_keyboard_listener_t axidev_io_keyboard_listener_create(void);

/**
 * @brief Destroy a Listener instance.
 * @param listener Listener handle to destroy (safe to call with NULL).
 */
AXIDEV_IO_API void
axidev_io_keyboard_listener_destroy(axidev_io_keyboard_listener_t listener);

/**
 * @brief Start the listener. The callback may be invoked from an internal
 * thread.
 * @param listener Listener handle.
 * @param cb Callback invoked for each observed event.
 * @param user_data Opaque pointer forwarded to the callback.
 * @return true on success; false if the listener could not be started
 *         (for example due to missing permissions or platform support).
 */
AXIDEV_IO_API bool
axidev_io_keyboard_listener_start(axidev_io_keyboard_listener_t listener,
                                axidev_io_keyboard_listener_cb cb,
                                void *user_data);

/**
 * @brief Stop the listener. Safe to call from any thread; a no-op if not
 * running.
 * @param listener Listener handle.
 */
AXIDEV_IO_API void
axidev_io_keyboard_listener_stop(axidev_io_keyboard_listener_t listener);

/**
 * @brief Query whether the listener is currently active.
 * @param listener Listener handle.
 * @return true if listening; false otherwise.
 */
AXIDEV_IO_API bool
axidev_io_keyboard_listener_is_listening(axidev_io_keyboard_listener_t listener);
/** @} */ /* end of Listener group */

/* ---------------- Utilities / Conversions ---------------- */

/**
 * @brief Convert a Key to a heap-allocated, null-terminated string.
 * @param key Key to convert.
 * @return char* Heap-allocated canonical name for the key (caller must free
 * with `axidev_io_free_string`), or NULL on allocation failure.
 */
AXIDEV_IO_API char *axidev_io_keyboard_key_to_string(axidev_io_keyboard_key_t key);

/**
 * @brief Parse a textual key name to a `axidev_io_keyboard_key_t` value.
 * @param name Null-terminated string (case-insensitive; accepts common aliases
 * like "esc", "space").
 * @return axidev_io_keyboard_key_t Parsed key value, or 0 (Key::Unknown) for
 * unknown/invalid inputs.
 */
AXIDEV_IO_API axidev_io_keyboard_key_t axidev_io_keyboard_string_to_key(const char *name);

/**
 * @brief Convert a KeyWithModifier to a heap-allocated, null-terminated string.
 *
 * Returns a string that includes modifier prefixes, e.g., "Shift+A" or
 * "Ctrl+C".
 *
 * @param key_mod Key and modifier state to convert.
 * @return char* Heap-allocated string with modifiers and key (caller must free
 * with `axidev_io_free_string`), or NULL on allocation failure.
 */
AXIDEV_IO_API char *axidev_io_keyboard_key_to_string_with_modifier(
    axidev_io_keyboard_key_with_modifier_t key_mod);

/**
 * @brief Parse a key combo string (e.g., "Shift+A", "Ctrl+Shift+C") into
 * a KeyWithModifier.
 *
 * @param combo Null-terminated string with optional modifier prefixes.
 * @param out_key_mod Output pointer for the parsed key and modifiers (must not
 * be NULL).
 * @return true on successful parse; false if combo is NULL or parsing fails.
 */
AXIDEV_IO_API bool axidev_io_keyboard_string_to_key_with_modifier(
    const char *combo, axidev_io_keyboard_key_with_modifier_t *out_key_mod);

/**
 * @brief Get the library version string.
 * @return const char* Pointer to an internal, null-terminated version string
 * (do not free).
 */
AXIDEV_IO_API const char *axidev_io_library_version(void);

/**
 * @brief Retrieve the last process-wide error string, if any.
 *
 * Many functions record a process-global "last error" string on failure.
 * The returned string is heap-allocated and must be freed with
 * `axidev_io_free_string`. Returns NULL if there is no last error.
 *
 * @return char* Heap-allocated string with the last error message, or NULL.
 */
AXIDEV_IO_API char *axidev_io_get_last_error(void);

/**
 * @brief Clear the process-wide last error string, if any.
 */
AXIDEV_IO_API void axidev_io_clear_last_error(void);

/**
 * @brief Free a string returned by the C API.
 * @param s Pointer obtained from functions like `axidev_io_keyboard_key_to_string` or
 * `axidev_io_get_last_error`. Safe to call with NULL.
 */
AXIDEV_IO_API void axidev_io_free_string(char *s);

/** @name Logging
 * @brief Functions to control and use the internal logging system.
 *
 * The library includes a lightweight header-only logging utility. These
 * functions allow C API users to control logging behavior at runtime.
 * @{
 */

/**
 * @brief Log level enumeration for the library's logging facility.
 *
 * Levels are ordered from most verbose (Debug) to least (Error).
 */
enum {
  AXIDEV_IO_LOG_LEVEL_DEBUG = 0, /**< Debug level (most verbose) */
  AXIDEV_IO_LOG_LEVEL_INFO = 1,  /**< Info level */
  AXIDEV_IO_LOG_LEVEL_WARN = 2,  /**< Warning level */
  AXIDEV_IO_LOG_LEVEL_ERROR = 3, /**< Error level (least verbose) */
};

/**
 * @typedef axidev_io_log_level_t
 * @brief Type for log level values.
 */
typedef uint8_t axidev_io_log_level_t;

/**
 * @brief Set the global logging level.
 *
 * Controls which log messages are emitted by the library. Messages at or above
 * the specified level will be displayed; lower-priority messages are
 * suppressed.
 *
 * @param level Log level to set (one of AXIDEV_IO_LOG_LEVEL_*).
 */
AXIDEV_IO_API void axidev_io_log_set_level(axidev_io_log_level_t level);

/**
 * @brief Get the current global logging level.
 * @return axidev_io_log_level_t The current log level (one of
 * AXIDEV_IO_LOG_LEVEL_*).
 */
AXIDEV_IO_API axidev_io_log_level_t axidev_io_log_get_level(void);

/**
 * @brief Check whether messages at a specific level are currently enabled.
 * @param level Log level to test (one of AXIDEV_IO_LOG_LEVEL_*).
 * @return true if messages at this level will be emitted; false otherwise.
 */
AXIDEV_IO_API bool axidev_io_log_is_enabled(axidev_io_log_level_t level);

/**
 * @brief Emit a log message (internal use; consider using macros instead).
 *
 * This function is thread-safe and suitable for direct use, but C consumers
 * typically use the helper macros `AXIDEV_IO_LOG_DEBUG`, `AXIDEV_IO_LOG_INFO`,
 * `AXIDEV_IO_LOG_WARN`, or `AXIDEV_IO_LOG_ERROR` instead.
 *
 * @param level Log level for the message.
 * @param file Source file name (typically `__FILE__`).
 * @param line Source line number (typically `__LINE__`).
 * @param fmt Printf-style format string.
 * @param ... Variadic arguments for the format string.
 */
AXIDEV_IO_API void axidev_io_log_message(axidev_io_log_level_t level,
                                     const char *file, int line,
                                     const char *fmt, ...);

/** @} */ /* end of Logging group */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXIDEV_IO_C_API_H */
