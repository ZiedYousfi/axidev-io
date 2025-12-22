#pragma once

// typr-io - typr_io.hpp
// Umbrella public header.
//
// This single-include header pulls together the stable public surface that
// consumers commonly need:
//
//   - <typr-io/core.hpp>    : core enums/types/version macros
//   - <typr-io/sender.hpp>  : Sender (input injection) API
//   - <typr-io/listener.hpp>: Listener (global input monitoring) API
//
// Projects that need only a subset of the API are encouraged to include the
// specific headers directly, but this file is convenient for quick prototyping.

#include <typr-io/core.hpp>
#include <typr-io/listener.hpp>
#include <typr-io/sender.hpp>

namespace typr {
namespace io {

/// Convenience access to the library version string (mirrors TYPR_IO_VERSION).
inline const char *libraryVersion() noexcept { return TYPR_IO_VERSION; }

} // namespace io
} // namespace typr
