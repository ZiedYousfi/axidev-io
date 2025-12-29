# Consumer Guide

This document is for application authors who want to use `typr-io` to inject input or listen for keyboard output in their apps. It focuses on quickstarts, common usage patterns, examples and runtime caveats.

## Quickstart — build & install

Build the project (Release shared library):

```typr-io/docs/consumers/README.md#L1-6
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DTYPR_IO_BUILD_SHARED=ON
cmake --build . -- -j
# optionally install:
# cmake --install . --prefix /usr/local
```

CMake usage in your project:

```typr-io/docs/consumers/README.md#L8-12
find_package(typr-io CONFIG REQUIRED)
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE typr::io)
```

Include the specific headers for the public API you need:

```
#include <typr-io/keyboard/sender.hpp>
#include <iostream>

int main() {
  typr::io::keyboard::Sender sender;
  auto caps = sender.capabilities();
  std::cout << "canInjectKeys: " << caps.canInjectKeys << "\n";

  if (caps.canInjectText) {
    sender.typeText("Hello from typr-io");
  } else if (caps.canInjectKeys) {
    sender.tap(typr::io::keyboard::Key::A);
  }
  return 0;
}
```

## Common usage patterns

- Check `capabilities()` at runtime to decide whether to:
  - call `typeText()` for direct Unicode injection, or
  - use `tap()` / `keyDown()` + `keyUp()` for physical key events.
- Use `combo(mods, key)` to safely perform shortcuts (it will hold modifiers, tap the key, then release modifiers).
- Use `setKeyDelay()` to tune the timing of `tap`/`combo` if necessary for fragile apps.

## Examples & test harness

- Look at `examples/` for small example programs demonstrating typical usage.
- `test_consumer/` contains a lightweight consumer used by the project's test targets. It's useful for smoke-testing your environment and understanding how the public API behaves on your platform.

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
  - `TYPR_IO_LOG_LEVEL=debug|info|warn|error`
    Example: `TYPR_IO_LOG_LEVEL=info` limits output to Info and above.
- Legacy compatibility: if `TYPR_IO_LOG_LEVEL` is not set, the legacy `TYPR_OSK_DEBUG_BACKEND` env var is still recognized (non-zero enables debug logging, `0` disables). Prefer `TYPR_IO_LOG_LEVEL` for explicit control.
- You can also change the log level programmatically from code by calling `typr::io::log::setLevel(typr::io::log::Level::Info)` (include `<typr-io/log.hpp>`).
- For macOS permission issues, check System Settings → Privacy & Security → Accessibility / Input Monitoring and confirm your app has been granted access.
- For uinput permission problems on Linux, ensure your udev rule is installed and the running user is in the correct group, then re-login or reload udev rules.

## Best practices

- Prefer the highest-fidelity option available:
  - If `canInjectKeys` is available, prefer physical key events for correct shortcut and modifier semantics.
  - If only text injection is required and `canInjectText` is available, `typeText()` is convenient.
- Use `combo()` for common shortcuts to avoid modifier state mistakes.
- Keep UI timers and delays conservative — some target apps may need slightly longer delays between key events.

## Where to go next

- Read `docs/developers/README.md` for build details, platform backend internals, and information on how to extend `typr-io`.
- Browse the public headers at `include/typr-io/` (e.g., `include/typr-io/core.hpp`, `include/typr-io/sender.hpp`, `include/typr-io/listener.hpp`) for API reference and types.
- File issues or feature requests in the project's issue tracker if you encounter platform-specific behavior, layout mappings that don't match expectations, or missing capabilities.

If you have specific problems reproducing expected keyboard behavior on a platform (wrong character, missing modifier, layout mismatch), please include:

- OS and version
- Keyboard layout (e.g. UK, French AZERTY, Dvorak)
- Minimal reproduction steps or a small program

This helps us diagnose layout and permission-related problems faster. Happy typing!
