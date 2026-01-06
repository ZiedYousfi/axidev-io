#pragma once
/**
 * @file keyboard/common.hpp
 * @brief Core keyboard types and utilities for axidev::io::keyboard.
 *
 * This header defines logical key identifiers, modifier flags, backend
 * capability flags and small helper utilities used by both the `Sender`
 * (input injection) and `Listener` (global monitoring) subsystems.
 *
 * @note **API Design: KeyWithModifier is the consumer-facing type.**
 *
 * All communication between consumers and the library uses `KeyWithModifier`,
 * which combines a logical `Key` with the required `Modifier` flags. This
 * struct is the standard unit for representing keyboard input.
 *
 * The raw `Key` enum and `Modifier` enum are internal convenience types used
 * within `KeyWithModifier`. Consumers should not use `Key` directly in API
 * calls; instead, they should use `KeyWithModifier` (or the string parsing
 * functions like `stringToKeyWithModifier`).
 *
 * Similarly, `KeyMapping` is an internal structure used for platform-specific
 * keycode mappings and is not part of the public consumer API.
 */

#include <axidev-io/core.hpp>

#include <cstdint>
#include <string>

namespace axidev {
namespace io {
namespace keyboard {

/**
 * @enum Key
 * @brief Logical key identifiers (layout-agnostic).
 *
 * Stable numeric values are chosen to allow serialization and round-tripping.
 * These values represent logical keys, not platform-specific scan codes.
 *
 * @note This is an internal convenience type. Consumers should use
 *       `KeyWithModifier` to represent keys in API calls, not raw `Key` values.
 */
enum class Key : uint16_t {
  Unknown = 0,
  // Letters
  A = 1,
  B = 2,
  C = 3,
  D = 4,
  E = 5,
  F = 6,
  G = 7,
  H = 8,
  I = 9,
  J = 10,
  K = 11,
  L = 12,
  M = 13,
  N = 14,
  O = 15,
  P = 16,
  Q = 17,
  R = 18,
  S = 19,
  T = 20,
  U = 21,
  V = 22,
  W = 23,
  X = 24,
  Y = 25,
  Z = 26,

  // Numbers (main/top row)
  Num0 = 33,
  Num1 = 34,
  Num2 = 35,
  Num3 = 36,
  Num4 = 37,
  Num5 = 38,
  Num6 = 39,
  Num7 = 40,
  Num8 = 41,
  Num9 = 42,

  // Function keys
  F1 = 43,
  F2 = 44,
  F3 = 45,
  F4 = 46,
  F5 = 47,
  F6 = 48,
  F7 = 49,
  F8 = 50,
  F9 = 51,
  F10 = 52,
  F11 = 53,
  F12 = 54,
  F13 = 55,
  F14 = 56,
  F15 = 57,
  F16 = 58,
  F17 = 59,
  F18 = 60,
  F19 = 61,
  F20 = 62,

  // Control / editing
  Enter = 63,
  Escape = 64,
  Backspace = 65,
  Tab = 66,
  Space = 67,

  // Navigation
  Left = 68,
  Right = 69,
  Up = 70,
  Down = 71,
  Home = 72,
  End = 73,
  PageUp = 74,
  PageDown = 75,
  Delete = 76,
  Insert = 77,
  PrintScreen = 78,
  ScrollLock = 79,
  Pause = 80,

  // Numpad
  NumpadDivide = 83,
  NumpadMultiply = 84,
  NumpadMinus = 85,
  NumpadPlus = 86,
  NumpadEnter = 87,
  NumpadDecimal = 88,
  Numpad0 = 89,
  Numpad1 = 90,
  Numpad2 = 91,
  Numpad3 = 92,
  Numpad4 = 93,
  Numpad5 = 94,
  Numpad6 = 95,
  Numpad7 = 96,
  Numpad8 = 97,
  Numpad9 = 98,

  // Modifiers
  ShiftLeft = 99,
  ShiftRight = 100,
  CtrlLeft = 101,
  CtrlRight = 102,
  AltLeft = 103,
  AltRight = 104,
  SuperLeft = 105,
  SuperRight = 106,
  CapsLock = 107,
  NumLock = 108,

  // Misc
  Help = 109,
  Menu = 110,
  Power = 111,
  Sleep = 112,
  Wake = 113,
  Mute = 114,
  VolumeDown = 115,
  VolumeUp = 116,
  MediaPlayPause = 117,
  MediaStop = 118,
  MediaNext = 119,
  MediaPrevious = 120,
  BrightnessDown = 121,
  BrightnessUp = 122,
  Eject = 123,

  // Common punctuation (layout-dependent physical positions)
  Grave = 124,
  Minus = 125,
  Equal = 126,
  LeftBracket = 127,
  RightBracket = 128,
  Backslash = 129,
  Semicolon = 130,
  Apostrophe = 131,
  Comma = 132,
  Period = 133,
  Slash = 134,

  // Additional symbol keys (useful to represent shifted characters explicitly)
  // Shifted / symbol characters (useful to represent shifted characters
  // explicitly)
  At = 135,
  Hashtag = 136,
  Exclamation = 137,
  Dollar = 138,
  Percent = 139,
  Caret = 140,
  Ampersand = 141,
  Asterisk = 142,
  LeftParen = 143,
  RightParen = 144,
  Underscore = 145,
  Plus = 146,
  Colon = 147,
  Quote = 148,
  QuestionMark = 149,
  Bar = 150,
  LessThan = 151,
  GreaterThan = 152,

  // ASCII control characters (C0 controls 0x00-0x1F) and DEL (0x7F).
  // These provide canonical enum values for raw ASCII control bytes (for
  // example, '\x03' -> ETX). Where a logical key already exists (e.g.,
  // Backspace, Tab, Enter, Escape, Delete) the ASCII name is aliased to the
  // existing logical key to preserve canonical names and interoperability.
  AsciiNUL = 160,             // 0x00
  AsciiSOH = 161,             // 0x01
  AsciiSTX = 162,             // 0x02
  AsciiETX = 163,             // 0x03
  AsciiEOT = 164,             // 0x04
  AsciiENQ = 165,             // 0x05
  AsciiACK = 166,             // 0x06
  AsciiBell = 167,            // 0x07 (BEL)
  AsciiBackspace = Backspace, // 0x08 (BS)
  AsciiTab = Tab,             // 0x09 (HT)
  AsciiLF = Enter,            // 0x0A (LF) - mapped to Enter
  AsciiVT = 171,              // 0x0B (VT)
  AsciiFF = 172,              // 0x0C (FF)
  AsciiCR = Enter,            // 0x0D (CR) - mapped to Enter
  AsciiSO = 174,              // 0x0E (SO)
  AsciiSI = 175,              // 0x0F (SI)
  AsciiDLE = 176,             // 0x10 (DLE)
  AsciiDC1 = 177,             // 0x11 (DC1)
  AsciiDC2 = 178,             // 0x12 (DC2)
  AsciiDC3 = 179,             // 0x13 (DC3)
  AsciiDC4 = 180,             // 0x14 (DC4)
  AsciiNAK = 181,             // 0x15 (NAK)
  AsciiSYN = 182,             // 0x16 (SYN)
  AsciiETB = 183,             // 0x17 (ETB)
  AsciiCAN = 184,             // 0x18 (CAN)
  AsciiEM = 185,              // 0x19 (EM)
  AsciiSUB = 186,             // 0x1A (SUB)
  AsciiEscape = Escape,       // 0x1B (ESC)
  AsciiFS = 188,              // 0x1C (FS)
  AsciiGS = 189,              // 0x1D (GS)
  AsciiRS = 190,              // 0x1E (RS)
  AsciiUS = 191,              // 0x1F (US)
  AsciiDEL = Delete,          // 0x7F (DEL)

  // Additional keys commonly present on X11 / XF86 keyboards and international
  // layouts. These are appended with explicit values to keep enum layout
  // stable for serialization.
  NumpadEqual = 192,
  Degree = 193,
  Sterling = 194,
  Mu = 195,
  PlusMinus = 196,
  DeadCircumflex = 197,
  DeadDiaeresis = 198,
  Section = 199,
  Cancel = 200,
  Redo = 201,
  Undo = 202,
  Find = 203,
  Hangul = 204,
  HangulHanja = 205,
  Katakana = 206,
  Hiragana = 207,
  Henkan = 208,
  Muhenkan = 209,
  OE = 210,
  oe = 211,
  SunProps = 212,
  SunFront = 213,
  Copy = 214,
  Open = 215,
  Paste = 216,
  Cut = 217,
  Calculator = 218,
  Explorer = 219,
  Phone = 220,
  WebCam = 221,
  AudioRecord = 222,
  AudioRewind = 223,
  AudioPreset = 224,
  Messenger = 225,
  Search = 226,
  Go = 227,
  Finance = 228,
  Game = 229,
  Shop = 230,
  HomePage = 231,
  Reload = 232,
  Close = 233,
  Send = 234,
  Xfer = 235,
  LaunchA = 236,
  LaunchB = 237,
  Launch1 = 238,
  Launch2 = 239,
  Launch3 = 240,
  Launch4 = 241,
  Launch5 = 242,
  Launch6 = 243,
  Launch7 = 244,
  Launch8 = 245,
  Launch9 = 246,
  TouchpadToggle = 247,
  TouchpadOn = 248,
  TouchpadOff = 249,
  KbdLightOnOff = 250,
  KbdBrightnessDown = 251,
  KbdBrightnessUp = 252,
  Mail = 253,
  MailForward = 254,
  Save = 255,
  Documents = 256,
  Battery = 257,
  Bluetooth = 258,
  WLAN = 259,
  UWB = 260,
  Next_VMode = 261,
  Prev_VMode = 262,
  MonBrightnessCycle = 263,
  BrightnessAuto = 264,
  DisplayOff = 265,
  WWAN = 266,
  RFKill = 267,
};

/**
 * @enum Modifier
 * @brief Modifier bitmask flags (type-safe enum class).
 *
 * Use bitwise operators (provided below) to compose and test modifier masks.
 */
enum class Modifier : uint8_t {
  None = 0,
  Shift = 0x01,
  Ctrl = 0x02,
  Alt = 0x04,
  Super = 0x08,
  CapsLock = 0x10,
  NumLock = 0x20,
};

/**
 * @brief Bitwise OR operator for Modifier flags.
 * @param a First operand.
 * @param b Second operand.
 * @return Modifier Combined flags containing bits from both operands.
 */
inline Modifier operator|(Modifier a, Modifier b) {
  return static_cast<Modifier>(static_cast<uint8_t>(a) |
                               static_cast<uint8_t>(b));
}
inline Modifier operator&(Modifier a, Modifier b) {
  return static_cast<Modifier>(static_cast<uint8_t>(a) &
                               static_cast<uint8_t>(b));
}
inline Modifier &operator|=(Modifier &a, Modifier b) {
  a = a | b;
  return a;
}
inline Modifier &operator&=(Modifier &a, Modifier b) {
  a = a & b;
  return a;
}
/**
 * @brief Check whether @p flag is present in @p state.
 *
 * A convenience helper that tests modifier bit flags in a type-safe way.
 *
 * @param state Modifier bitmask to inspect.
 * @param flag Modifier flag to test for.
 * @return true if @p flag is set in @p state.
 */
inline bool hasModifier(Modifier state, Modifier flag) {
  return (static_cast<uint8_t>(state) & static_cast<uint8_t>(flag)) != 0;
}

/**
 * @struct KeyWithModifier
 * @brief Associates a logical Key with the modifiers required to produce it.
 *
 * **This is the consumer-facing type for all keyboard operations.**
 *
 * All Sender methods and Listener callbacks use `KeyWithModifier` to represent
 * keyboard input. This struct combines a logical `Key` with the `Modifier`
 * flags that should be applied.
 *
 * Examples:
 * - `{Key::A, Modifier::None}` - lowercase 'a' (no modifiers)
 * - `{Key::A, Modifier::Shift}` - uppercase 'A'
 * - `{Key::C, Modifier::Ctrl}` - Ctrl+C
 * - `{Key::S, Modifier::Ctrl | Modifier::Shift}` - Ctrl+Shift+S
 *
 * Use `stringToKeyWithModifier("Ctrl+C")` for convenient parsing from strings.
 */
struct KeyWithModifier {
  Key key{Key::Unknown};                 ///< The base key
  Modifier requiredMods{Modifier::None}; ///< Modifiers needed to produce this

  /// Returns true if the key is valid (not Unknown).
  bool isValid() const { return key != Key::Unknown; }

  /// Default constructor creates an invalid key.
  KeyWithModifier() = default;

  /// Construct with a key and optional modifiers.
  KeyWithModifier(Key k, Modifier mods = Modifier::None)
      : key(k), requiredMods(mods) {}

  /// Equality comparison.
  bool operator==(const KeyWithModifier &other) const {
    return key == other.key && requiredMods == other.requiredMods;
  }

  bool operator!=(const KeyWithModifier &other) const {
    return !(*this == other);
  }
};

/**
 * @struct KeyMapping
 * @brief Internal structure for platform keycode mappings.
 *
 * Associates a platform keycode with the modifiers required to produce
 * a specific character or Key.
 *
 * @note This is an internal type used for keyboard layout discovery and
 *       platform-specific keycode mappings. It is not part of the public
 *       consumer API. Consumers should use `KeyWithModifier` instead.
 *
 * When discovering keyboard layout mappings, characters like '!' or '@' require
 * holding Shift (and sometimes other modifiers). This structure captures both
 * the base keycode and the required modifier state, allowing the Sender to
 * correctly synthesize keystrokes and enabling the Listener to understand
 * which modifiers were needed to produce a given input.
 *
 * The `keycode` field is platform-specific:
 * - macOS: CGKeyCode (uint16_t)
 * - Windows: Virtual-key code (WORD, uint16_t)
 * - Linux: evdev keycode (int, stored as int32_t for consistency)
 *
 * Additionally, `producedKey` stores the logical Key that this mapping
 * produces. This allows reverse lookups from (keycode, modifiers) -> Key.
 *
 * @note This structure is used internally by the keymap initialization
 *       functions on each platform.
 */
struct KeyMapping {
  int32_t keycode{-1}; ///< Platform-specific keycode (-1 = invalid)
  Modifier requiredMods{
      Modifier::None}; ///< Modifiers needed to produce the character
  Key producedKey{Key::Unknown}; ///< The logical Key this mapping produces

  /// Returns true if the mapping is valid (has a non-negative keycode).
  bool isValid() const { return keycode >= 0; }

  /// Default constructor creates an invalid mapping.
  KeyMapping() = default;

  /// Construct a mapping with a keycode and optional modifiers.
  KeyMapping(int32_t code, Modifier mods = Modifier::None)
      : keycode(code), requiredMods(mods), producedKey(Key::Unknown) {}

  /// Construct a mapping with a keycode, modifiers, and produced key.
  KeyMapping(int32_t code, Modifier mods, Key key)
      : keycode(code), requiredMods(mods), producedKey(key) {}
};

// Backend capabilities description - describes what a backend / sender can do.
/**
 * @struct Capabilities
 * @brief Describes features supported or required by a keyboard Sender backend.
 *
 * Each boolean member indicates whether a backend supports a particular
 * capability or requires a platform permission/feature. Inspect these via
 * `Sender::capabilities()` before calling backend-specific helpers.
 */
struct Capabilities {
  bool canInjectKeys{false};  // can send physical key events
  bool canInjectText{false};  // can inject arbitrary Unicode text
  bool canSimulateHID{false}; // true hardware-level simulation (uinput, etc.)
  bool supportsKeyRepeat{false};
  bool needsAccessibilityPerm{false};
  bool needsInputMonitoringPerm{false};
  bool needsUinputAccess{false};
};

// Backend type / platform descriptor (which implementation is active)
/**
 * @enum BackendType
 * @brief Backend/platform descriptor for the active keyboard sender
 * implementation.
 *
 * Indicates which platform-specific backend is active for keyboard input
 * injection.
 */
enum class BackendType : uint8_t {
  Unknown,
  Windows,
  MacOS,
  LinuxLibinput,
  LinuxUInput,
};

// Utility conversion helpers - implemented in a single translation unit.
// These are exported so consumer code (or tests) can call them directly.
/**
 * @brief Convert a Key to its canonical textual name.
 * @param key Logical key to convert.
 * @return std::string Canonical name for the key (e.g., "A", "Enter").
 */
AXIDEV_IO_API std::string keyToString(Key key);
/**
 * @brief Parse a textual key name into a Key value.
 * @param str Input string (case-insensitive; accepts common aliases).
 * @return Key Parsed key value or Key::Unknown for unrecognized strings.
 */
AXIDEV_IO_API Key stringToKey(const std::string &str);

/**
 * @brief Convert a Key with its required modifiers to a human-readable string.
 *
 * For keys that require modifiers (like '!' which needs Shift+1), this returns
 * a string that includes the modifier prefix. For example:
 * - Key::A with Modifier::None -> "A" (or "a" for lowercase)
 * - Key::Num1 with Modifier::Shift -> "Shift+1" (produces '!')
 *
 * @param key Logical key to convert.
 * @param mods Modifiers required to produce this key.
 * @return std::string Human-readable string with modifiers and key.
 */
AXIDEV_IO_API std::string keyToStringWithModifier(Key key, Modifier mods);

/**
 * @brief Parse a textual key name that may include modifier prefixes.
 *
 * This function accepts strings like "Shift+A", "Ctrl+Shift+B", or plain "A".
 * The modifiers are parsed and returned along with the base key.
 *
 * @param str Input string (case-insensitive; accepts modifier prefixes).
 * @return KeyWithModifier Parsed key and required modifiers.
 */
AXIDEV_IO_API KeyWithModifier stringToKeyWithModifier(const std::string &str);

} // namespace keyboard
} // namespace io
} // namespace axidev
