#pragma once

/**
 * @file keyboard/sender.hpp
 * @brief Keyboard input injection API (cross-platform).
 *
 * This header declares the `axidev::io::keyboard::Sender` class that provides
 * layout-aware physical key injection and text injection where supported by the
 * platform.
 *
 * @note **API Design: KeyWithModifier is the consumer-facing type.**
 *
 * All public sender methods accept `KeyWithModifier` to represent a key
 * combined with its required modifiers. This ensures consistent, unambiguous
 * key input across the API. The raw `Key` enum is an internal convenience type
 * and should not be used directly by consumers; always pair a `Key` with its
 * `Modifier` using `KeyWithModifier`.
 *
 * @par Usage:
 * @code{.cpp}
 * #include <axidev-io/keyboard/sender.hpp>
 *
 * int main() {
 *   using namespace axidev::io::keyboard;
 *   Sender sender;
 *   if (sender.capabilities().canInjectKeys) {
 *     // Tap 'A' with no modifiers
 *     sender.tap({Key::A, Modifier::None});
 *     // Tap Shift+A (uppercase 'A')
 *     sender.tap({Key::A, Modifier::Shift});
 *     // Ctrl+C combo
 *     sender.tap({Key::C, Modifier::Ctrl});
 *   }
 *   return 0;
 * }
 * @endcode
 */

#include <cstdint>
#include <memory>
#include <string>

#include <axidev-io/keyboard/common.hpp>

namespace axidev {
namespace io {
namespace keyboard {

/**
 * @class Sender
 * @brief Layout-aware input sender (keyboard injection)
 *
 * Provides a compact, cross-platform API to inject keys and text. The
 * implementation is platform-specific and hidden in the pimpl (`Impl`) type.
 */
class AXIDEV_IO_API Sender {
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
   *
   * The modifiers in `keyMod.requiredMods` are automatically pressed before the
   * key and tracked for release when `keyUp` is called.
   *
   * @param keyMod Key with modifiers to press.
   * @return true on success; false on failure.
   */
  bool keyDown(KeyWithModifier keyMod);

  /**
   * @brief Simulate a physical key release.
   *
   * Releases the key and any modifiers that were specified in `keyMod`.
   *
   * @param keyMod Key with modifiers to release.
   * @return true on success; false on failure.
   */
  bool keyUp(KeyWithModifier keyMod);

  /**
   * @brief Convenience: press and release a key with its modifiers.
   *
   * This is the primary method for sending a key event. It handles:
   * 1. Pressing the required modifiers
   * 2. Pressing and releasing the key
   * 3. Releasing the modifiers
   *
   * @param keyMod Key with modifiers to tap.
   * @return true on success; false on failure.
   */
  bool tap(KeyWithModifier keyMod);

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
  /**
   * @brief Internal helper to send a raw key event without modifier handling.
   *
   * This is used internally by keyDown/keyUp/tap and the modifier helpers.
   * Not part of the public API.
   *
   * @param key Raw key to send.
   * @param down True for key press, false for key release.
   * @return true on success; false on failure.
   */
  bool sendRawKey(Key key, bool down);

  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace keyboard
} // namespace io
} // namespace axidev
