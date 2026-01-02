#pragma once
/**
 * @file core.hpp
 * @brief Core library version and export macros for axidev::io.
 *
 * This header defines library version information and symbol export macros
 * used throughout the axidev-io library.
 *
 * For keyboard-specific types (Key, Modifier, Capabilities, BackendType),
 * include `<axidev-io/keyboard/common.hpp>` instead.
 */

#ifndef AXIDEV_IO_VERSION
// Default version; CMake can override these by defining AXIDEV_IO_VERSION_* via
// -D flags if desired.
#define AXIDEV_IO_VERSION "0.3.1"
#define AXIDEV_IO_VERSION_MAJOR 0
#define AXIDEV_IO_VERSION_MINOR 3
#define AXIDEV_IO_VERSION_PATCH 1
#endif

// Symbol export macro to support building shared libraries on Windows.
// CMake configures `axidev_io_EXPORTS` when building the shared target.
// For static builds we expose `AXIDEV_IO_STATIC` so headers avoid using
// __declspec(dllimport) which would make defining functions invalid on MSVC.
#ifndef AXIDEV_IO_API
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(axidev_io_EXPORTS)
#define AXIDEV_IO_API __declspec(dllexport)
#elif defined(AXIDEV_IO_STATIC)
#define AXIDEV_IO_API
#else
#define AXIDEV_IO_API __declspec(dllimport)
#endif
#else
#if defined(__GNUC__) && (__GNUC__ >= 4)
#define AXIDEV_IO_API __attribute__((visibility("default")))
#else
#define AXIDEV_IO_API
#endif
#endif
#endif

namespace axidev {
namespace io {

/**
 * @brief Convenience access to the library version string (mirrors
 * AXIDEV_IO_VERSION).
 * @return const char* Null-terminated version string (statically allocated).
 */
inline const char *libraryVersion() noexcept { return AXIDEV_IO_VERSION; }

} // namespace io
} // namespace axidev
