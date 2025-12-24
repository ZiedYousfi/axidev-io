#pragma once

// typr-io - listener.hpp
// Public Listener (formerly OutputListener) API.
//
// The Listener provides a cross-platform, best-effort global keyboard event
// monitoring facility. It invokes a user-supplied callback for each observed
// key event with a produced Unicode codepoint (0 if none), the logical Key,
// the active Modifier bitmask, and whether the event was a press (true) or
// a release (false).
//
// Note on timing and character delivery:
// - The delivered `codepoint` is computed from raw key events and represents
//   the Unicode character produced at the time of that low-level event. On
//   some platforms (notably Windows low-level hooks) the character computed
//   for a key press may differ from the character observed by the focused
//   application or terminal (which commonly receives the character on key
//   release). This can produce mismatches if consumers only observe press
//   events.
// - Consumers that want to reliably capture the characters visible to the
//   focused application or terminal/STDIN should consider handling characters
//   on key release (when `pressed == false`). The Listener provides both press
//   and release events so callers can choose the behaviour that best fits
//   their needs.
// - The codepoint mapping is intentionally lightweight and does not implement
//   full IME / dead-key composition; it is a best-effort mapping for common
//   printable characters.
//
// Example:
//
//   #include <typr-io/listener.hpp>
//
//   int main() {
//     typr::io::Listener l;
//     bool ok = l.start([](char32_t cp, typr::io::Key k, typr::io::Modifier m,
//     bool pressed) {
//       // handle event
//     });
//     if (!ok) {
//       // Listener couldn't be started (missing permissions / platform
//       support)
//     }
//     // ...
//     l.stop();
//     return 0;
//   }
//
#include <functional>
#include <memory>

#include <typr-io/core.hpp>

namespace typr {
namespace io {

class TYPR_IO_API Listener {
public:
  // Callback signature:
  //   - codepoint: produced Unicode codepoint (0 if none). This is computed for
  //     the low-level key event and may differ between press and release on
  //     some platforms.
  //   - key: logical Key (Key::Unknown if unknown)
  //   - mods: current modifier state
  //   - pressed: true for key press, false for key release
  // Note: consumers that want to reliably observe the character actually sent
  // to the focused application or terminal should consider handling events on
  // key release (pressed == false).
  using Callback = std::function<void(char32_t codepoint, Key key,
                                      Modifier mods, bool pressed)>;

  Listener();
  ~Listener();

  Listener(const Listener &) = delete;
  Listener &operator=(const Listener &) = delete;
  Listener(Listener &&) noexcept;
  Listener &operator=(Listener &&) noexcept;

  // Start listening for global keyboard events. Returns true on success.
  // The callback may be invoked from an internal thread. The listener attempts
  // to be low noise and will fail to start when platform support or permissions
  // aren't available.
  bool start(Callback cb);

  // Stop listening. Safe to call from any thread. If the listener failed to
  // start, this is a no-op.
  void stop();

  // Whether the listener is currently active.
  [[nodiscard]] bool isListening() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace io
} // namespace typr
