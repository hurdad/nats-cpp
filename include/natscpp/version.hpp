#pragma once

#include <cstdint>

// natscpp's own version. CMake reads the project version from these three lines, so this header is the one place
// to change it. (For the nats.c version, see natscpp::nats_GetVersion() in <natscpp/library.hpp>.)
// Macros rather than constants so they work in #if.
// NOLINTBEGIN(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)
#define NATSCPP_VERSION_MAJOR 0
#define NATSCPP_VERSION_MINOR 2
#define NATSCPP_VERSION_PATCH 0
// NOLINTEND(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#define NATSCPP_VERSION_STRINGIFY_(x) #x
#define NATSCPP_VERSION_STRINGIFY(x) NATSCPP_VERSION_STRINGIFY_(x)
// "MAJOR.MINOR.PATCH", e.g. "0.2.0".
#define NATSCPP_VERSION_STRING                     \
  NATSCPP_VERSION_STRINGIFY(NATSCPP_VERSION_MAJOR) \
  "." NATSCPP_VERSION_STRINGIFY(NATSCPP_VERSION_MINOR) "." NATSCPP_VERSION_STRINGIFY(NATSCPP_VERSION_PATCH)
// The same encoding as nats.c's NATS_VERSION_NUMBER: (major << 16) | (minor << 8) | patch, e.g. 0x000200.
#define NATSCPP_VERSION_NUMBER ((NATSCPP_VERSION_MAJOR << 16) | (NATSCPP_VERSION_MINOR << 8) | NATSCPP_VERSION_PATCH)

namespace natscpp {

// The natscpp version these headers are, as "MAJOR.MINOR.PATCH".
[[nodiscard]] constexpr const char* version() noexcept { return NATSCPP_VERSION_STRING; }

// The natscpp version as (major << 16) | (minor << 8) | patch, comparable like NATS_VERSION_NUMBER.
[[nodiscard]] constexpr std::uint32_t version_number() noexcept { return NATSCPP_VERSION_NUMBER; }

}  // namespace natscpp
