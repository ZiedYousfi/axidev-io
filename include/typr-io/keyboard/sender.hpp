#pragma once

/**
 * @file keyboard/sender.hpp
 * @brief Keyboard input injection API (cross-platform).
 *
 * This header declares the `typr::io::keyboard::Sender` class that provides
 * layout-aware physical key injection and text injection where supported by the
 * platform.
 *
 * @par Usage:
 * @code{.cpp}
 * #include <typr-io/keyboard/sender.hpp>
 *
 * int main() {
 *   typr::io::keyboard::Sender sender;
 *   if (sender.capabilities().canInjectKeys) {
 *     sender.tap(typr::io::keyboard::Key::A);
 *   }
 *   return 0;
 * }
 * @endcode
 */

#include <cstdint>
#include <memory>
#include <string>

#include <typr-io/keyboard/common.hpp>

namespace typr {
namespace io {
namespace keyboard {

/**
 * @class Sender
 * @brief Layout-aware input sender (keyboard injection)
 *
 * Provides a compact, cross-platform API to inject keys and text. The
 * implementation is platform-specific and hidden in the pimpl (`Impl`) type.
 */
class TYPR_IO_API Sender {
public:
  /**
   * @brief Construct a new Sender instance.
   */
  Sender();

  /**
   * @brief Destroy the Sender instance and release resources.
   */
  ~Sender();

  // Non-copyable, movable
  Sender(const Sender &) = delete;
  Sender &operator=(const Sender &) = delete;
  Sender(Sender &&) noexcept;
  Sender &operator=(Sender &&) noexcept;

  // --- Info ---
  /**
   * @brief Return the active backend type.
   * @return BackendType Active backend implementation.
   */
  [[nodiscard]] BackendType type() const;

  /**
   * @brief Return the capabilities of the active backend.
   * @return Capabilities Backend capabilities.
   */
  [[nodiscard]] Capabilities capabilities() const;

  /**
   * @brief Check whether the sender backend is ready to inject input.
   * @return true if the backend is ready; false otherwise.
   */
  [[nodiscard]] bool isReady() const;

  /**
   * @brief Attempt to request any runtime permissions required by the backend.
   * @return true if the backend is ready after requesting permissions.
   */
  bool requestPermissions();

  // --- Physical key events ---
  /**
   * @brief Simulate a physical key press and keep it pressed until `keyUp` is
   * called.
   * @param key Logical key to press.
   * @return true on success; false on failure.
   */
  bool keyDown(Key key);

  /**
   * @brief Simulate a physical key release.
   * @param key Logical key to release.
   * @return true on success; false on failure.
   */
  bool keyUp(Key key);

  /**
   * @brief Convenience: press and release a key with a small delay.
   * @param key Logical key to tap.
   * @return true on success; false on failure.
   */
  bool tap(Key key);

  // --- Modifier helpers ---
  /**
   * @brief Return the currently active modifier mask.
   * @return Modifier Active modifiers (bitmask).
   */
  [[nodiscard]] Modifier activeModifiers() const;

  /**
   * @brief Press the requested modifier keys (prefers left-side variants when
   * available).
   * @param mod Modifier mask to hold.
   * @return true on success; false on failure.
   */
  bool holdModifier(Modifier mod);

  /**
   * @brief Release the requested modifier keys.
   * @param mod Modifier mask to release.
   * @return true on success; false on failure.
   */
  bool releaseModifier(Modifier mod);

  /**
   * @brief Release all tracked modifiers.
   * @return true on success; false on failure.
   */
  bool releaseAllModifiers();

  /**
   * @brief Execute a key combo: press modifiers, tap key, release modifiers.
   * @param mods Modifier mask to hold while tapping the key.
   * @param key Key to tap.
   * @return true on success; false on failure.
   */
  bool combo(Modifier mods, Key key);

  // --- Text injection ---
  /**
   * @brief Inject Unicode text directly (layout-independent).
   * @param text Unicode text (UTF-32) to inject.
   * @return true on success; false if unsupported or on failure.
   */
  bool typeText(const std::u32string &text);

  /**
   * @brief Convenience overload that accepts UTF-8 text.
   * @param utf8Text UTF-8 encoded string to inject.
   * @return true on success; false if unsupported or on failure.
   */
  bool typeText(const std::string &utf8Text);

  /**
   * @brief Inject a single Unicode codepoint.
   * @param codepoint Unicode codepoint to inject.
   * @return true on success; false on failure.
   */
  bool typeCharacter(char32_t codepoint);

  // --- Misc ---
  /**
   * @brief Flush pending events to ensure timely delivery.
   */
  void flush();

  /**
   * @brief Set the key delay used by tap/combo operations.
   * @param delayUs Delay in microseconds.
   */
  void setKeyDelay(uint32_t delayUs);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace keyboard
} // namespace io
} // namespace typr
