#pragma once

// typr-io - sender.hpp
// Public Sender (formerly InputBackend) API.
// This header declares the `typr::io::Sender` class that provides layout-aware
// physical key injection and text injection where supported by the platform.
//
// Usage:
//   #include <typr-io/sender.hpp>
//   typr::io::Sender sender;
//   if (sender.capabilities().canInjectKeys) sender.tap(typr::io::Key::A);

#include <cstdint>
#include <memory>
#include <string>

#include <typr-io/core.hpp>

namespace typr {
namespace io {

/// Sender - layout-aware input sender (keyboard injection)
///
/// Provides a compact, cross-platform API to inject keys and text. The
/// implementation is platform-specific and hidden in the pimpl (`Impl`) type.
class TYPR_IO_API Sender {
public:
  Sender();
  ~Sender();

  // Non-copyable, movable
  Sender(const Sender &) = delete;
  Sender &operator=(const Sender &) = delete;
  Sender(Sender &&) noexcept;
  Sender &operator=(Sender &&) noexcept;

  // --- Info ---
  [[nodiscard]] BackendType type() const;
  [[nodiscard]] Capabilities capabilities() const;
  [[nodiscard]] bool isReady() const;

  /// Try to request runtime permissions (where applicable).
  /// Returns true if the backend is ready to inject after this call.
  bool requestPermissions();

  // --- Physical key events ---
  /// Simulate a physical key press and keep it pressed until `keyUp` is
  /// called. Returns true on success.
  bool keyDown(Key key);
  /// Simulate a key release. Returns true on success.
  bool keyUp(Key key);

  /// Convenience: keyDown + small delay + keyUp.
  bool tap(Key key);

  // --- Modifier helpers ---
  [[nodiscard]] Modifier activeModifiers() const;

  /// Press the requested modifier keys (prefers left-side variants when
  /// available). Returns true on success.
  bool holdModifier(Modifier mod);
  /// Release the requested modifier keys (if currently held). Returns true on
  /// success.
  bool releaseModifier(Modifier mod);
  /// Release all tracked modifiers.
  bool releaseAllModifiers();

  /// Execute a key combo: press modifiers, tap key, release modifiers.
  bool combo(Modifier mods, Key key);

  // --- Text injection ---
  /// Inject Unicode text directly (layout-independent). Returns true on
  /// success. Not all backends support this.
  bool typeText(const std::u32string &text);
  /// Convenience overload that accepts UTF-8.
  bool typeText(const std::string &utf8Text);
  /// Inject a single Unicode codepoint.
  bool typeCharacter(char32_t codepoint);

  // --- Misc ---
  /// Force sync/flush of pending events.
  void flush();

  /// Set the delay (microseconds) used by tap/combo.
  void setKeyDelay(uint32_t delayUs);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace io
} // namespace typr
