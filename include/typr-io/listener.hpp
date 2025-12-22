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
  //   - codepoint: produced Unicode codepoint (0 if none)
  //   - key: logical Key (Key::Unknown if unknown)
  //   - mods: current modifier state
  //   - pressed: true for key press, false for key release
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
