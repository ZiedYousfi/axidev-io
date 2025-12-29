#pragma once
/**
 * @file core.hpp
 * @brief Core library version and export macros for typr::io.
 *
 * This header defines library version information and symbol export macros
 * used throughout the typr-io library.
 *
 * For keyboard-specific types (Key, Modifier, Capabilities, BackendType),
 * include `<typr-io/keyboard/common.hpp>` instead.
 */

#include <cstdint>

#ifndef TYPR_IO_VERSION
// Default version; CMake can override these by defining TYPR_IO_VERSION_* via
// -D flags if desired.
#define TYPR_IO_VERSION "0.3.0"
#define TYPR_IO_VERSION_MAJOR 0
#define TYPR_IO_VERSION_MINOR 3
#define TYPR_IO_VERSION_PATCH 0
#endif

// Symbol export macro to support building shared libraries on Windows.
// CMake configures `typr_io_EXPORTS` when building the shared target.
// For static builds we expose `TYPR_IO_STATIC` so headers avoid using
// __declspec(dllimport) which would make defining functions invalid on MSVC.
#ifndef TYPR_IO_API
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(typr_io_EXPORTS)
#define TYPR_IO_API __declspec(dllexport)
#elif defined(TYPR_IO_STATIC)
#define TYPR_IO_API
#else
#define TYPR_IO_API __declspec(dllimport)
#endif
#else
#if defined(__GNUC__) && (__GNUC__ >= 4)
#define TYPR_IO_API __attribute__((visibility("default")))
#else
#define TYPR_IO_API
#endif
#endif
#endif

namespace typr {
namespace io {

/**
 * @brief Convenience access to the library version string (mirrors
 * TYPR_IO_VERSION).
 * @return const char* Null-terminated version string (statically allocated).
 */
inline const char *libraryVersion() noexcept { return TYPR_IO_VERSION; }

} // namespace io
} // namespace typr
