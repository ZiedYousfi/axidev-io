# axidev-io

A lightweight C++ library for cross-platform keyboard input injection and monitoring.

## Documentation

Documentation is split by audience in the `docs/` directory:

- Consumers: `docs/consumers/README.md` — quickstart, examples, and usage notes.
- Developers: `docs/developers/README.md` — build instructions, architecture notes, testing, and contributing.

Start with the document that matches your goal.

## Quickstart

CMake (consumer) usage:

```cpp
cmake_minimum_required(VERSION 3.15)
find_package(axidev-io CONFIG REQUIRED)
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE axidev::io)
```

Minimal usage example:

```cpp
#include <axidev-io/keyboard/sender.hpp>

int main() {
  using namespace axidev::io::keyboard;
  Sender sender;
  if (sender.capabilities().canInjectKeys) {
    // All operations use KeyWithModifier
    sender.tap({Key::A, Modifier::None});     // Tap 'a'
    sender.tap({Key::A, Modifier::Shift});    // Tap 'A' (uppercase)
    sender.tap({Key::C, Modifier::Ctrl});     // Ctrl+C

    // Or parse from string
    auto combo = stringToKeyWithModifier("Ctrl+S");
    sender.tap(combo);
  }
  return 0;
}
```

## C API (C wrapper)

A compact C ABI is available for consumers that want to bind axidev-io from other
languages. The C API header is installed as `<axidev-io/c_api.h>` and provides
opaque handle types for keyboard `Sender` and `Listener`, simple helpers to convert key
names, and a small set of functions to control keyboard sending and listening from C.

Basic usage (C):

```c
#include <axidev-io/c_api.h>
#include <stdio.h>

/* Example keyboard listener callback */
static void my_cb(uint32_t codepoint, axidev_io_keyboard_key_with_modifier_t key_mod,
                  bool pressed, void *ud) {
  (void)ud;
  (void)codepoint; /* Prioritize key_mod for portability */
  char *name = axidev_io_keyboard_key_to_string_with_modifier(key_mod);
  printf("key=%s %s\n", name ? name : "?", pressed ? "pressed" : "released");
  if (name) axidev_io_free_string(name);
}

int main(void) {
  axidev_io_keyboard_sender_t sender = axidev_io_keyboard_sender_create();
  if (!sender) {
    char *err = axidev_io_get_last_error();
    if (err) {
      fprintf(stderr, "keyboard sender create: %s\n", err);
      axidev_io_free_string(err);
    }
    return 1;
  }

  axidev_io_keyboard_capabilities_t caps;
  axidev_io_keyboard_sender_get_capabilities(sender, &caps);
  if (caps.can_inject_keys) {
    /* All operations use axidev_io_keyboard_key_with_modifier_t */
    axidev_io_keyboard_key_with_modifier_t key_mod;

    /* Tap 'A' key */
    axidev_io_keyboard_string_to_key_with_modifier("A", &key_mod);
    axidev_io_keyboard_sender_tap(sender, key_mod);

    /* Ctrl+C combo */
    axidev_io_keyboard_string_to_key_with_modifier("Ctrl+C", &key_mod);
    axidev_io_keyboard_sender_tap(sender, key_mod);
  } else if (caps.can_inject_text) {
    axidev_io_keyboard_sender_type_text_utf8(sender, "Hello from C API\n");
  }

  axidev_io_keyboard_sender_destroy(sender);

  /* Keyboard listener example (may require platform permissions) */
  axidev_io_keyboard_listener_t listener = axidev_io_keyboard_listener_create();
  if (listener && axidev_io_keyboard_listener_start(listener, my_cb, NULL)) {
    /* ... application runs ... */
    axidev_io_keyboard_listener_stop(listener);
  }
  axidev_io_keyboard_listener_destroy(listener);

  return 0;
}
```

Notes:

- Strings returned by the C API are heap-allocated and must be freed with
  `axidev_io_free_string`.
- Keyboard listener callbacks may be invoked from an internal thread; your callback
  must be thread-safe.
- Use `axidev_io_get_last_error()` to retrieve a heap-allocated error message if
  a function fails (free it with `axidev_io_free_string`).

An example C program is available at `examples/example_c.c`. Build it with:

```bash
mkdir build
cd build
cmake .. -DAXIDEV_IO_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Building from source

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Contributing

- Update `docs/consumers/` or `docs/developers/` for user- or developer-facing changes.
- Add tests or examples to `examples/` where relevant.
- Open a pull request with a clear description and focused changes.

## License

See the `LICENSE` file in the project root.

## Reporting issues

Open issues in the project's issue tracker and include reproduction steps, platform, and any relevant logs or details.
