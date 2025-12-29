# typr-io

A lightweight C++ library for cross-platform keyboard input injection and monitoring.

## Documentation

Documentation is split by audience in the `docs/` directory:

- Consumers: `docs/consumers/README.md` — quickstart, examples, and usage notes.
- Developers: `docs/developers/README.md` — build instructions, architecture notes, testing, and contributing.

Start with the document that matches your goal.

## Quickstart

CMake (consumer) usage:

```typr-io/README.md#L1-4
find_package(typr-io CONFIG REQUIRED)
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE typr::io)
```

Minimal usage example:

```typr-io/README.md#L6-12
#include <typr-io/keyboard/sender.hpp>

int main() {
  typr::io::keyboard::Sender sender;
  if (sender.capabilities().canInjectKeys) {
    sender.tap(typr::io::keyboard::Key::A);
  }
  return 0;
}
```

## C API (C wrapper)

A compact C ABI is available for consumers that want to bind typr-io from other
languages. The C API header is installed as `<typr-io/c_api.h>` and provides
opaque handle types for keyboard `Sender` and `Listener`, simple helpers to convert key
names, and a small set of functions to control keyboard sending and listening from C.

Basic usage (C):

```c
#include <typr-io/c_api.h>
#include <stdio.h>

/* Example keyboard listener callback */
static void my_cb(uint32_t codepoint, typr_io_keyboard_key_t key,
                  typr_io_keyboard_modifier_t mods, bool pressed, void *ud) {
  (void)ud;
  char *name = typr_io_keyboard_key_to_string(key);
  printf("key=%s codepoint=%u mods=0x%02x %s\n", name ? name : "?", (unsigned)codepoint,
         (unsigned)mods, pressed ? "pressed" : "released");
  if (name) typr_io_free_string(name);
}

int main(void) {
  typr_io_keyboard_sender_t sender = typr_io_keyboard_sender_create();
  if (!sender) {
    char *err = typr_io_get_last_error();
    if (err) {
      fprintf(stderr, "keyboard sender create: %s\n", err);
      typr_io_free_string(err);
    }
    return 1;
  }

  typr_io_keyboard_capabilities_t caps;
  typr_io_keyboard_sender_get_capabilities(sender, &caps);
  if (caps.can_inject_keys) {
    typr_io_keyboard_sender_tap(sender, typr_io_keyboard_string_to_key("A"));
  } else if (caps.can_inject_text) {
    typr_io_keyboard_sender_type_text_utf8(sender, "Hello from C API\n");
  }

  typr_io_keyboard_sender_destroy(sender);

  /* Keyboard listener example (may require platform permissions) */
  typr_io_keyboard_listener_t listener = typr_io_keyboard_listener_create();
  if (listener && typr_io_keyboard_listener_start(listener, my_cb, NULL)) {
    /* ... application runs ... */
    typr_io_keyboard_listener_stop(listener);
  }
  typr_io_keyboard_listener_destroy(listener);

  return 0;
}
```

Notes:

- Strings returned by the C API are heap-allocated and must be freed with
  `typr_io_free_string`.
- Keyboard listener callbacks may be invoked from an internal thread; your callback
  must be thread-safe.
- Use `typr_io_get_last_error()` to retrieve a heap-allocated error message if
  a function fails (free it with `typr_io_free_string`).

An example C program is available at `examples/example_c.c`. Build it with:

```bash
mkdir build
cd build
cmake .. -DTYPR_IO_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Building from source

```typr-io/README.md#L14-18
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Contributing

- Update `docs/consumers/` or `docs/developers/` for user- or developer-facing changes.
- Add tests or examples to `examples/` and `test_consumer/` where relevant.
- Open a pull request with a clear description and focused changes.

## License

See the `LICENSE` file in the project root.

## Reporting issues

Open issues in the project's issue tracker and include reproduction steps, platform, and any relevant logs or details.
