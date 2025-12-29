#pragma once

/**
 * @file keyboard/listener.hpp
 * @brief Global keyboard event Listener (cross-platform).
 *
 * The Listener provides a cross-platform, best-effort global keyboard event
 * monitoring facility. It invokes a user-supplied callback for each observed
 * key event with a computed Unicode codepoint (0 if none), the logical Key,
 * the active Modifier bitmask, and whether the event is a press (true) or
 * a release (false).
 *
 * Note on timing and character delivery:
 * - The delivered `codepoint` is computed from raw key events and represents
 *   the Unicode character produced at the time of that low-level event. On
 *   some platforms (notably Windows low-level hooks) the character computed
 *   for a key press may differ from the character observed by the focused
 *   application or terminal (which commonly receives the character on key
 *   release). This can produce mismatches if consumers only observe press
 *   events.
 * - Consumers that want to reliably capture the characters visible to the
 *   focused application or terminal/STDIN should consider handling characters
 *   on key release (when `pressed == false`). The Listener provides both press
 *   and release events so callers can choose the behaviour that best fits
 *   their needs.
 * - The codepoint mapping is intentionally lightweight and does not implement
 *   full IME / dead-key composition; it is a best-effort mapping for common
 *   printable characters.
 *
 * Example:
 *
 * @code{.cpp}
 * #include <typr-io/keyboard/listener.hpp>
 *
 * int main() {
 *   typr::io::keyboard::Listener l;
 *   bool ok = l.start([](char32_t cp, typr::io::keyboard::Key k,
 *                        typr::io::keyboard::Modifier m, bool pressed) {
 *     // handle event
 *   });
 *   if (!ok) {
 *     // Listener couldn't be started (missing permissions/platform support)
 *   }
 *   // ...
 *   l.stop();
 *   return 0;
 * }
 * @endcode
 */
#include <functional>
#include <memory>

#include <typr-io/keyboard/common.hpp>

namespace typr {
namespace io {
namespace keyboard {

/**
 * @class Listener
 * @brief Global keyboard event monitoring facility.
 *
 * Listener provides a cross-platform, best-effort API to observe global
 * keyboard events. Use `start()` to begin receiving events and `stop()` to
 * end listening. Callbacks may be invoked on an internal background thread,
 * therefore they must be thread-safe.
 */
class TYPR_IO_API Listener {
public:
  /**
   * @brief Callback invoked for each observed key event.
   *
   * @param codepoint Unicode codepoint produced by the event (0 if none).
   *                  This value is computed from the low-level event and may
   *                  differ between press and release on some platforms.
   * @param key Logical key identifier (Key::Unknown if unknown).
   * @param mods Current modifier state (Modifiers bitmask).
   * @param pressed True for key press, false for key release.
   *
   * @note Consumers that need the character actually delivered to the focused
   *       application or terminal should consider handling events on key
   *       release (when `pressed == false`).
   */
  using Callback = std::function<void(char32_t codepoint, Key key,
                                      Modifier mods, bool pressed)>;

  Listener();
  ~Listener();

  Listener(const Listener &) = delete;
  Listener &operator=(const Listener &) = delete;
  Listener(Listener &&) noexcept;
  Listener &operator=(Listener &&) noexcept;

  /**
   * @brief Start listening for global keyboard events.
   *
   * The provided callback may be invoked from an internal thread. The listener
   * will fail to start when platform support or permissions aren't available.
   *
   * @param cb Callback to invoke for each observed event.
   * @return true on success, false on failure.
   */
  bool start(Callback cb);

  /**
   * @brief Stop listening for global keyboard events.
   *
   * Safe to call from any thread. If the listener is not running this call is
   * a no-op.
   */
  void stop();

  /**
   * @brief Check whether the listener is currently active.
   * @return true when the listener is listening, false otherwise.
   */
  [[nodiscard]] bool isListening() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace keyboard
} // namespace io
} // namespace typr
