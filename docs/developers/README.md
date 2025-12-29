# Developer Guide

This document is for maintainers and contributors who want to work on `typr-io`. It explains the repository layout, how to build and test locally, how to add or modify platform backends (senders/listeners), debugging tips, and the recommended PR workflow.

Table of contents

- [Quickstart](#quickstart)
- [Repository layout](#repository-layout)
- [Build & development workflow](#build--development-workflow)
- [Debugging & logging](#debugging--logging)
- [Adding a new backend (sender or listener)](#adding-a-new-backend-sender-or-listener)
- [Testing & CI](#testing--ci)
- [Packaging & release notes](#packaging--release-notes)
- [Contribution checklist](#contribution-checklist)
- [Troubleshooting & platform notes](#troubleshooting--platform-notes)
- [Key files & references](#key-files--references)

## Quickstart

If you're diving in for the first time, here's the minimal flow I use when starting work:

```sh
# configure (Debug by default)
make configure

# build the library (and examples if enabled)
make build

# run the lightweight tests
make test
```

You can also use `cmake` directly:

```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DTYPR_IO_BUILD_EXAMPLES=ON
cmake --build . -- -j
```

If you rely on editor tooling (clangd), run:

```sh
make export-compile-commands
# or make configure + build; the compile_commands.json lives in build
```

## Repository layout

Top-level important paths (brief):

- `CMakeLists.txt` — top-level build configuration and exported targets.
- `Makefile` — convenience targets like `configure`, `build`, `test`, `run-consumer`.
- `include/typr-io/` — public headers (e.g., `include/typr-io/core.hpp`, `include/typr-io/keyboard/common.hpp`, `include/typr-io/keyboard/sender.hpp`, `include/typr-io/keyboard/listener.hpp`).
- `src/`:
  - `src/keyboard/sender/` — platform input injection (HID / virtual keyboard) implementations (e.g. `sender_macos.mm`, `sender_windows.cpp`, `sender_uinput.cpp`).
  - `src/keyboard/listener/` — global output listener implementations (`listener_macos.mm`, `listener_windows.cpp`, `listener_linux.cpp`).
  - `src/keyboard/common/` — shared keyboard utilities (key-to-string mappings, etc.).
- `examples/` — example programs demonstrating consumer usage.
- `test_consumer/` — lightweight test consumer used in `make test`.
- Packaging manifests: `conanfile.py`, `vcpkg.json`.

## Build & development workflow

- Use `make configure` to configure the project and generate the `compile_commands.json` used by clangd.
- Toggle options during `cmake` configure:
  - `-DTYPR_IO_BUILD_EXAMPLES=ON` — build examples.
  - `-DTYPR_IO_BUILD_SHARED=ON` — build shared library variant.
  - `-DBACKEND_USE_X11=ON` — platform-specific feature toggles (example).
- Use `make build` (or `cmake --build`) to build.
- Run `make test` to run the project's smoke tests.
- Use the `Makefile` targets or the `.zed/tasks.json` for editor-integrated tasks if you use Zed.

## Debugging & logging

- Logging is enabled by default at the Debug level (most verbose) for the time being. Logged messages include an ISO-like timestamp, severity, file:line, thread id, and the formatted message.
- Control runtime logging with the environment variable (preferred):
  - `TYPR_IO_LOG_LEVEL=debug|info|warn|error`
    Example: `TYPR_IO_LOG_LEVEL=info` limits output to Info and above.
- Programmatic control: you can also change the log level at runtime from code by calling `typr::io::log::setLevel(typr::io::log::Level::Info)` (include `<typr-io/log.hpp>`).
- Legacy: `TYPR_OSK_DEBUG_BACKEND=0|1` is still recognized historically, but `TYPR_IO_LOG_LEVEL` is the preferred mechanism. When `TYPR_IO_LOG_LEVEL` is not set logging defaults to Debug (enabled).
- Quick debugging:
  - To enable very verbose logs for local debugging: `TYPR_IO_LOG_LEVEL=debug`
- Platform-specific tips:
  - macOS: check System Settings → Privacy & Security (Accessibility / Input Monitoring).
  - Linux: the listener uses `libinput` + `xkbcommon` as the canonical Linux listener and these packages are required at configure time. Ensure the `libinput`, `libudev`, and `xkbcommon` development packages are installed (pkg-config detects them during configure) and that the running user has appropriate runtime privileges (for example, membership in the `input` group) to access `/dev/input/event*` devices.
  - Windows: use system event logs and verify `GetKeyboardLayout` usage in debug builds.
- For layout or mapping issues, include OS version, keyboard layout, and short reproduction steps when opening issues.

## Adding a new backend (sender or listener)

When adding support for a new platform, follow this checklist:

1. Decide where it belongs:
   - Input injection backends go in `src/keyboard/sender/` (e.g. `sender_<platform>.[cpp|mm]`).
   - Global output listener backends go in `src/keyboard/listener/` (e.g. `listener_<platform>.[cpp|mm]`).
2. Use the existing platform implementations as a template (see `sender_macos.mm`, `sender_windows.cpp`, `sender_uinput.cpp`, `listener_linux.cpp`).
3. Keep platform-specific code out of shared headers; prefer implementation files and conditional build rules.
4. Add the new source file to the CMake target:
   - Guard it with a platform check if required (e.g. `if(APPLE)` / `if(WIN32)` / `if(UNIX)`).
5. Implement and expose capabilities correctly:
   - Ensure `capabilities()` reflects whether your backend supports `canInjectKeys`, `canInjectText`, `canSimulateHID`, etc.
   - Ensure `isReady()` accurately reports readiness (permissions, device present).
6. Add tests/examples:
   - Add a small example showing the typical usage and, when possible, a test in `test_consumer/` or new test target that can run headless.
7. Document:
   - Update `docs/developers/README.md` with platform-specific caveats and any runtime permission instructions.
   - Update `docs/consumers/README.md` if consumers need to do anything special (permissions, runtime flags).
8. Verify:
   - Run `make build` and `make test`.
   - Validate permission and runtime behavior on the target OS.

Listener vs Sender considerations

- The listener focuses on monitoring key events produced by the user (global keyboard output); it should be lightweight and avoid complex IME handling where possible.
- The sender is responsible for injecting input; prefer sending physical key events (scan codes / virtual keys) so that OS-level shortcuts and layout semantics are preserved. If physical events are not possible, implement reliable `typeText()` fallback for Unicode text injection.

## Testing & CI

- `make test` runs the repository's smoke tests (currently exercises the test consumer / example).
- Integration tests are separated from the lightweight unit tests because they exercise OS integration and may be interactive, require permissions/GUI, or take longer to run.
- To build and run integration tests locally:
  - Enable integration tests at configure time:
    - `cmake -S . -B build -DTYPR_IO_BUILD_TESTS=ON -DTYPR_IO_BUILD_INTEGRATION_TESTS=ON -DCMAKE_BUILD_TYPE=Debug`
    - `cmake --build build --parallel`
  - Run the integration test binary directly:
    - `./build/tests/typr-io-integration-tests`
  - Or run via CTest:
    - `ctest --test-dir build -R typr-io-integration-tests -C Debug --output-on-failure`
  - Useful environment variables (may be respected by integration tests or local helpers):
    - `TYPR_IO_RUN_INTEGRATION_TESTS=1` : historically used to gate integration tests.
    - `TYPR_IO_AUTO_CONFIRM=1` : auto-confirm interactive prompts for non-interactive runs (CI).
    - `TYPR_IO_INTERACTIVE=1` : enable interactive prompts when running locally.
- Keep tests platform-neutral where possible. For platform-specific behavior, provide small example-based tests and mark them so CI or maintainers can choose when to execute them.
- When adding critical behavior, add a test (or smoke example) that reproduces the issue so regressions are less likely.

## Packaging & release notes

- The repository includes `conanfile.py` and `vcpkg.json` to help with packaging.
- The public target exported by `CMakeLists.txt` is intended for `find_package(typr-io CONFIG REQUIRED)`.
- When preparing a release:
  - Update version metadata and the public headers if necessary.
  - Add release notes describing platform caveats and any API changes.
  - Ensure tests and examples build on the supported platforms.

## Contribution checklist

Before opening a PR:

- [ ] Run `make configure && make build && make test`.
- [ ] Add or update unit / example tests that cover your change where appropriate.
- [ ] Update docs in `docs/consumers/` or `docs/developers/` when behavior or developer workflows change.
- [ ] Describe the motivation and potential compatibility impact in the PR description.
- [ ] Run quick static checks with your editor (clangd, clang-tidy if applicable) and keep code style consistent with the surrounding codebase.

## Troubleshooting & platform notes

- macOS:
  - Accessibility and Input Monitoring permissions can block injection or listening. `requestPermissions()` can prompt for Accessibility permission where applicable, but some permissions require user action in Settings.
  - For local preparation, the repository includes a helper task ("Prepare integration tests (request permissions)") (see `.zed/tasks.json`) that runs `./build/typr_io_consumer --request-permissions` to trigger runtime permission prompts after building the consumer.
  - Use `.mm` files for Objective-C++ code that needs macOS system APIs.
- Linux:
  - uinput requires correct `/dev/uinput` permissions. Common guidance:
    - Add a udev rule: `KERNEL=="uinput", MODE="0660", GROUP="input"`
    - Add your user to the `input` group and reload udev rules (or re-login).
  - `sender_uinput.cpp` implements the uinput device-based injection.
- Windows:
  - VK-code scanning and Unicode injection are handled by `sender_windows.cpp`. Ensure `GetKeyboardLayout`, `MapVirtualKeyExW`, and `ToUnicodeEx` behavior matches expectations on localized layouts.

## Key files & references

- `include/typr-io/` — public headers (e.g., `include/typr-io/core.hpp`, `include/typr-io/keyboard/common.hpp`, `include/typr-io/keyboard/sender.hpp`, `include/typr-io/keyboard/listener.hpp`).
- `src/keyboard/sender/` — input injection implementations.
- `src/keyboard/listener/` — global output listener implementations.
- `examples/` — sample apps demonstrating consumer APIs.
- `test_consumer/` — lightweight test harness used by `make test`.
- `CMakeLists.txt`, `Makefile`, `conanfile.py`, `vcpkg.json` — build and packaging helpers.
- `docs/consumers/README.md` — consumer-focused docs.
- `docs/developers/README.md` — this file (developer docs).

If you need help with a platform-specific problem, please open an issue and include:

- OS and version,
- Keyboard layout (e.g., `en-US`, `fr-FR (AZERTY)`, `dvorak`),
- Short reproduction steps (a small program or sequence that demonstrates the problem),
- Any relevant debug logging (set `TYPR_OSK_DEBUG_BACKEND=1`).

Thanks for contributing — clear documentation, small focused PRs, and tests/examples go a long way toward making the project easy to maintain.
