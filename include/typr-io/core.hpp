#pragma once

// typr-io - core.hpp
// Core types and utilities used by the typr-io Sender and Listener
// Public API now lives under namespace `typr::io`.
//
// This header is intentionally minimal and stable: it exposes logical key
// identifiers, modifiers, backend capability flags, and small helpers used
// by both the sender (input injection) and listener (global monitoring)
// subsystems.

#include <cstdint>
#include <string>

#ifndef TYPR_IO_VERSION
// Default version; CMake can override these by defining TYPR_IO_VERSION_* via
// -D flags if desired.
#define TYPR_IO_VERSION "0.1.0"
#define TYPR_IO_VERSION_MAJOR 0
#define TYPR_IO_VERSION_MINOR 1
#define TYPR_IO_VERSION_PATCH 0
#endif

// Symbol export macro to support building shared libraries on Windows.
// CMake configures `typr_io_EXPORTS` when building the shared target.
#ifndef TYPR_IO_API
#if defined(_WIN32)
#if defined(typr_io_EXPORTS)
#define TYPR_IO_API __declspec(dllexport)
#else
#define TYPR_IO_API __declspec(dllimport)
#endif
#else
#define TYPR_IO_API
#endif
#endif

namespace typr {
namespace io {

// Logical key enum: stable numeric values so they can be serialized in
// configuration files. This enum is layout-agnostic (it represents logical
// keys, not platform scan codes).
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
};

// Modifier bitmask (use enum class for type safety but provide bit ops).
enum class Modifier : uint8_t {
  None = 0,
  Shift = 0x01,
  Ctrl = 0x02,
  Alt = 0x04,
  Super = 0x08,
  CapsLock = 0x10,
  NumLock = 0x20,
};

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
inline bool hasModifier(Modifier state, Modifier flag) {
  return (static_cast<uint8_t>(state) & static_cast<uint8_t>(flag)) != 0;
}

// Backend capabilities description - describes what a backend / sender can do.
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
enum class BackendType : uint8_t {
  Unknown,
  Windows,
  MacOS,
  LinuxX11,
  LinuxWayland,
  LinuxUInput,
};

// Utility conversion helpers - implemented in a single translation unit.
// These are exported so consumer code (or tests) can call them directly.
TYPR_IO_API std::string keyToString(Key key);
TYPR_IO_API Key stringToKey(const std::string &str);

} // namespace io
} // namespace typr
