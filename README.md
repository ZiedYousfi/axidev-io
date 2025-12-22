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
#include <typr-io/typr_io.hpp>

int main() {
  typr::io::Backend backend;
  if (backend.capabilities().canInjectKeys) {
    backend.tap(typr::io::Key::A);
  }
  return 0;
}
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
