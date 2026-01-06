# Consumer Guide

This document is for application authors who want to use `axidev-io` to inject input or listen for keyboard output in their apps. It focuses on quickstarts, common usage patterns, examples and runtime caveats.

## Important: KeyWithModifier is the Consumer-Facing Type

All communication between consumers and the library uses `KeyWithModifier` (C++) or `axidev_io_keyboard_key_with_modifier_t` (C). This struct combines a logical key with its required modifiers.

**Internal types (not for direct consumer use):**

- `Key` / `axidev_io_keyboard_key_t` - Raw key identifiers (internal convenience)
- `KeyMapping` - Platform-specific keycode mappings (internal)

**Consumer-facing type:**

- `KeyWithModifier` / `axidev_io_keyboard_key_with_modifier_t` - Always use this for API calls

Example:

```cpp
// C++ - Always use KeyWithModifier
sender.tap({Key::A, Modifier::None});       // Tap 'a'
sender.tap({Key::A, Modifier::Shift});      // Tap 'A' (uppercase)
sender.tap({Key::C, Modifier::Ctrl});       // Ctrl+C

// Or parse from string
auto kwm = stringToKeyWithModifier("Ctrl+Shift+S");
sender.tap(kwm);
```

```c
// C - Always use axidev_io_keyboard_key_with_modifier_t
axidev_io_keyboard_key_with_modifier_t key_mod;
axidev_io_keyboard_string_to_key_with_modifier("Ctrl+C", &key_mod);
axidev_io_keyboard_sender_tap(sender, key_mod);
```

## Quickstart — build & install

Build the project (Release shared library):

```axidev-io/docs/consumers/README.md#L1-6
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DAXIDEV_IO_BUILD_SHARED=ON
cmake --build . -- -j
# optionally install:
# cmake --install . --prefix /usr/local
```

CMake usage in your project:

```axidev-io/docs/consumers/README.md#L8-12
find_package(axidev-io CONFIG REQUIRED)
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE axidev::io)
```

Include the specific headers for the public API you need:

```cpp
#include <axidev-io/keyboard/sender.hpp>
#include <iostream>

int main() {
  using namespace axidev::io::keyboard;
  Sender sender;
  auto caps = sender.capabilities();
  std::cout << "canInjectKeys: " << caps.canInjectKeys << "\n";

  if (caps.canInjectText) {
    sender.typeText("Hello from axidev-io");
  } else if (caps.canInjectKeys) {
    // Use KeyWithModifier for all key operations
    sender.tap({Key::A, Modifier::None});          // Tap 'a'
    sender.tap({Key::A, Modifier::Shift});         // Tap 'A' (uppercase)
    sender.tap({Key::C, Modifier::Ctrl});          // Ctrl+C

    // Or parse from string for convenience
    auto saveCombo = stringToKeyWithModifier("Ctrl+S");
    sender.tap(saveCombo);
  }
  return 0;
}
```

## Common usage patterns

- Check `capabilities()` at runtime to decide whether to:
  - call `typeText()` for direct Unicode injection, or
  - use `tap()` with `KeyWithModifier` for physical key events.
- All key operations use `KeyWithModifier` - there's no separate `combo()` method.
- Use `setKeyDelay()` to tune the timing of `tap` if necessary for fragile apps.

### Modifier-aware key handling

The library provides modifier-aware APIs for working with key combinations:

- **`keyToStringWithModifier(key, mods)`** - Convert a key and modifiers to a human-readable string like `"Shift+A"` or `"Ctrl+C"`.
- **`stringToKeyWithModifier(str)`** - Parse strings like `"Shift+A"` or `"Ctrl+Shift+C"` into a `KeyWithModifier` struct containing both the key and required modifiers.
- **`KeyWithModifier`** struct - Pairs a `Key` with its required `Modifier` flags.

Example usage:

```cpp
#include <axidev-io/keyboard/common.hpp>
#include <axidev-io/keyboard/sender.hpp>
#include <iostream>

using namespace axidev::io::keyboard;

// Parse a combo string
auto kwm = stringToKeyWithModifier("Ctrl+Shift+S");
std::cout << "Key: " << keyToString(kwm.key) << "\n";        // "S"
std::cout << "Has Ctrl: " << hasModifier(kwm.requiredMods, Modifier::Ctrl) << "\n";  // true
std::cout << "Has Shift: " << hasModifier(kwm.requiredMods, Modifier::Shift) << "\n"; // true

// Convert back to string
std::string combo = keyToStringWithModifier(kwm.key, kwm.requiredMods);
std::cout << "Combo: " << combo << "\n";  // "Shift+Ctrl+S"

// Execute the combo using tap (handles modifiers automatically)
Sender sender;
sender.tap(kwm);
```

### Layout-aware key mapping

The library discovers keyboard layout mappings at runtime from the operating system. The `KeyMap` singleton provides access to these mappings:

```cpp
#include "keyboard/common/keymap.hpp"  // Internal header

using namespace axidev::io::keyboard;

// Get the singleton instance (auto-initializes on first use)
auto& km = KeyMap::instance();

// Look up what key+modifiers produce a character
auto kwm = km.keyForCharacter('!');  // Returns Key::Num1 + Modifier::Shift on US layout

// Check if a character can be typed
if (km.canTypeCharacter('@')) {
  auto mapping = km.mappingForCharacter('@');
  // mapping contains keycode and required modifiers
}
```

This is particularly useful for:

- Determining what key combination produces a given character on the current layout
- Building layout-independent text input systems
- Understanding modifier requirements for symbols that vary by keyboard layout

## Examples

- Look at `examples/` for small example programs demonstrating typical usage.

## Runtime caveats & platform notes

- macOS:
  - Accessibility and Input Monitoring permissions may be required for various features (injection and/or global monitoring).
  - Use `requestPermissions()` if you want to prompt the user for Accessibility permission.
- Linux:
  - The uinput backend needs access to `/dev/uinput`. Add a udev rule or run with appropriate permissions (adding your user to an `input` group is a common approach).
  - The uinput backend emits kernel-level key events and does not provide direct Unicode `typeText()` injection in the current implementation.
  - The listener implementation uses `libinput` + `xkbcommon` and reads events directly from input devices via udev. At build/configure time you must have the `libinput`, `libudev`, and `xkbcommon` development packages installed so pkg-config can find them. At runtime the listener typically requires membership in the `input` group or elevated privileges to access `/dev/input/event*` devices.
- Windows:
  - Typical user-level injection works; some advanced injection behaviors may be limited by system policy.

If a desired capability is not available on the target platform, use `capabilities()` and adapt your app's behavior (for instance falling back to composed key sequences or requesting the user to adjust permissions).

## Debugging & troubleshooting

- Logging is enabled by default at the Debug level (most verbose). Logs are printed to stderr and include an ISO-like timestamp, severity, file:line, thread id, and the formatted message.
- To control runtime verbosity, use the environment variable:
  - `AXIDEV_IO_LOG_LEVEL=debug|info|warn|error`
    Example: `AXIDEV_IO_LOG_LEVEL=info` limits output to Info and above.
- Legacy compatibility: if `AXIDEV_IO_LOG_LEVEL` is not set, the legacy `AXIDEV_OSK_DEBUG_BACKEND` env var is still recognized (non-zero enables debug logging, `0` disables). Prefer `AXIDEV_IO_LOG_LEVEL` for explicit control.
- You can also change the log level programmatically from code by calling `axidev::io::log::setLevel(axidev::io::log::Level::Info)` (include `<axidev-io/log.hpp>`).
- For macOS permission issues, check System Settings → Privacy & Security → Accessibility / Input Monitoring and confirm your app has been granted access.
- For uinput permission problems on Linux, ensure your udev rule is installed and the running user is in the correct group, then re-login or reload udev rules.

## Best practices

- Prefer the highest-fidelity option available:
  - If `canInjectKeys` is available, prefer physical key events for correct shortcut and modifier semantics.
  - If only text injection is required and `canInjectText` is available, `typeText()` is convenient.
- Use `combo()` for common shortcuts to avoid modifier state mistakes.
- Keep UI timers and delays conservative — some target apps may need slightly longer delays between key events.

## Where to go next

- Read `docs/developers/README.md` for build details, platform backend internals, and information on how to extend `axidev-io`.
- Browse the public headers at `include/axidev-io/` (e.g., `include/axidev-io/core.hpp`, `include/axidev-io/sender.hpp`, `include/axidev-io/listener.hpp`) for API reference and types.
- File issues or feature requests in the project's issue tracker if you encounter platform-specific behavior, layout mappings that don't match expectations, or missing capabilities.

If you have specific problems reproducing expected keyboard behavior on a platform (wrong character, missing modifier, layout mismatch), please include:

- OS and version
- Keyboard layout (e.g. UK, French AZERTY, Dvorak)
- Minimal reproduction steps or a small program

This helps us diagnose layout and permission-related problems faster. Happy typing!
