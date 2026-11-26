cmake_minimum_required(VERSION 3.20)

# The version lives in include/natscpp/version.hpp (natscpp::version()); read it from there so the two cannot differ.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/include/natscpp/version.hpp" _natscpp_version_lines
     REGEX "^#define NATSCPP_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(_part MAJOR MINOR PATCH)
  string(REGEX MATCH "NATSCPP_VERSION_${_part} ([0-9]+)" _ "${_natscpp_version_lines}")
  set(_natscpp_version_${_part} "${CMAKE_MATCH_1}")
  if(_natscpp_version_${_part} STREQUAL "")
    message(FATAL_ERROR "natscpp: NATSCPP_VERSION_${_part} not found in include/natscpp/version.hpp")
  endif()
endforeach()

project(natscpp
  VERSION ${_natscpp_version_MAJOR}.${_natscpp_version_MINOR}.${_natscpp_version_PATCH}
  DESCRIPTION "Modern header-only C++20 wrapper for nats.c"
  LANGUAGES CXX)

add_library(natscpp INTERFACE)
add_library(natscpp::natscpp ALIAS natscpp)

target_compile_features(natscpp INTERFACE cxx_std_20)

target_include_directories(natscpp
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

set(NATS_C_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nats.c")

# natscpp supports nats.c 3.15.0 and newer: the version bundled in third_party/nats.c.
set(NATSCPP_NATS_C_MIN_VERSION 3.15.0)

# nats.c backend selection:
#   auto    — use a system-installed cnats if it is NATSCPP_NATS_C_MIN_VERSION or newer; otherwise
#             fall back to the bundled third_party/nats.c submodule (default)
#   system  — require a system-installed cnats of at least NATSCPP_NATS_C_MIN_VERSION
#   bundled — always use the bundled submodule; error if submodule is absent
set(NATSCPP_NATS_BACKEND "auto" CACHE STRING
  "nats.c backend: auto | system | bundled")
set_property(CACHE NATSCPP_NATS_BACKEND PROPERTY STRINGS auto system bundled)

# Tests and examples are only built by default when natscpp is the top-level project, so that
# add_subdirectory(natscpp) does not add natscpp's own targets to a parent build.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(_natscpp_top_level ON)
  include(CTest)  # defines BUILD_TESTING (default ON)
  set(_natscpp_tests_default ${BUILD_TESTING})
else()
  set(_natscpp_top_level OFF)
  set(_natscpp_tests_default OFF)
endif()
option(NATSCPP_BUILD_TESTS "Build natscpp's tests" ${_natscpp_tests_default})
option(NATSCPP_BUILD_EXAMPLES "Build natscpp examples" ${_natscpp_top_level})
option(NATSCPP_WARNINGS_AS_ERRORS "Treat warnings in natscpp's own tests and examples as errors" OFF)

# ── Common dependencies ─────────────────────────────────────────────────────

find_package(Threads REQUIRED)
target_link_libraries(natscpp INTERFACE Threads::Threads)

find_package(OpenSSL QUIET)

# TLS for the bundled nats.c build: on by default when OpenSSL is available.
# (The system backend's TLS support is whatever that nats.c was built with.)
if(OpenSSL_FOUND)
  set(_natscpp_tls_default ON)
else()
  set(_natscpp_tls_default OFF)
endif()
option(NATSCPP_BUNDLED_WITH_TLS "Build the bundled nats.c with TLS (needs OpenSSL)" ${_natscpp_tls_default})

# ── Resolve the nats.c dependency ──────────────────────────────────────────

# Determine effective backend.
set(_natscpp_use_bundled OFF)
if(NATSCPP_NATS_BACKEND STREQUAL "bundled")
  set(_natscpp_use_bundled ON)
elseif(NATSCPP_NATS_BACKEND STREQUAL "system")
  set(_natscpp_use_bundled OFF)
else()  # auto: prefer a recent enough system nats.c, fall back to bundled
  find_package(cnats QUIET)
  if(cnats_FOUND AND cnats_VERSION VERSION_GREATER_EQUAL NATSCPP_NATS_C_MIN_VERSION)
    set(_natscpp_use_bundled OFF)
    message(STATUS "natscpp: system cnats ${cnats_VERSION} found — using system nats.c")
  elseif(cnats_FOUND)
    set(_natscpp_use_bundled ON)
    message(STATUS "natscpp: system cnats ${cnats_VERSION} is older than ${NATSCPP_NATS_C_MIN_VERSION} — "
                   "using bundled nats.c")
  else()
    set(_natscpp_use_bundled ON)
    message(STATUS "natscpp: system cnats not found — falling back to bundled nats.c")
  endif()
endif()

if(_natscpp_use_bundled)
  # ── Bundled submodule (static) ───────────────────────────────────────────

  # nats.c's options are set as normal variables, scoped to this configure step: nats.c requires CMake 3.13,
  # so its option() calls honor them (CMP0077) and nothing is forced into the parent project's cache.
  set(_natscpp_saved_build_testing "${BUILD_TESTING}")
  set(NATS_BUILD_LIB_SHARED OFF)
  set(NATS_BUILD_LIB_STATIC ON)
  set(NATS_BUILD_EXAMPLES   OFF)
  set(NATS_BUILD_STREAMING  OFF)
  if(NATSCPP_BUNDLED_WITH_TLS AND NOT OpenSSL_FOUND)
    message(FATAL_ERROR "NATSCPP_BUNDLED_WITH_TLS=ON but OpenSSL was not found")
  endif()
  set(NATS_BUILD_WITH_TLS ${NATSCPP_BUNDLED_WITH_TLS})
  set(NATSCPP_LINKS_OPENSSL ${NATSCPP_BUNDLED_WITH_TLS})
  set(_natscpp_has_tls ${NATSCPP_BUNDLED_WITH_TLS})
  message(STATUS "natscpp: bundled nats.c TLS support: ${NATSCPP_BUNDLED_WITH_TLS}")
  set(NATS_BUILD_WITH_DEPRECATED OFF)
  set(BUILD_TESTING OFF)  # nats.c's own test suite

  add_subdirectory("${NATS_C_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/nats.c")
  target_link_libraries(natscpp INTERFACE nats_static)
  set_target_properties(nats_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
  # nats.c is vendored code we do not patch: keep its warnings (e.g. GCC 15's -Wdiscarded-qualifiers) out of
  # our builds. natscpp's own code is built with full warnings (see _natscpp_warnings below).
  if(MSVC)
    target_compile_options(nats_static PRIVATE /w)
  else()
    target_compile_options(nats_static PRIVATE -w)
  endif()

  # nats.c's source tree has src/nats.h but no nats/nats.h, which is what
  # natscpp includes (and what an installed nats.c provides). Generate a
  # one-line forwarding header in the build tree, naming the bundled header by
  # absolute path so a system-installed nats.c of another version can't be
  # picked up instead.
  set(_natscpp_shim_dir "${CMAKE_CURRENT_BINARY_DIR}/natscpp-bundled-include")
  file(CONFIGURE OUTPUT "${_natscpp_shim_dir}/nats/nats.h"
    CONTENT "#pragma once\n#include \"${NATS_C_SOURCE_DIR}/src/nats.h\"\n")
  target_include_directories(natscpp
    INTERFACE $<BUILD_INTERFACE:${_natscpp_shim_dir}>)

  if(DEFINED _natscpp_saved_build_testing AND NOT _natscpp_saved_build_testing STREQUAL "")
    set(BUILD_TESTING "${_natscpp_saved_build_testing}")
  else()
    unset(BUILD_TESTING)
  endif()

else()
  # ── System nats.c via its cmake package ─────────────────────────────────
  # nats.c installs a cmake package named "cnats" for both shared and static
  # builds.  Prefer the shared target (cnats::nats); fall back to static.
  find_package(cnats REQUIRED)
  if(cnats_VERSION VERSION_LESS NATSCPP_NATS_C_MIN_VERSION)
    message(FATAL_ERROR "natscpp needs nats.c ${NATSCPP_NATS_C_MIN_VERSION} or newer, but the system cnats is "
                        "${cnats_VERSION}. Use -DNATSCPP_NATS_BACKEND=bundled (or auto) to build the bundled "
                        "nats.c, or install a newer one.")
  endif()
  # A static cnats does not carry its OpenSSL dependency, so link it when present.
  set(NATSCPP_LINKS_OPENSSL ${OpenSSL_FOUND})

  if(TARGET cnats::nats)
    target_link_libraries(natscpp INTERFACE cnats::nats)
    set(_natscpp_cnats_target cnats::nats)
    message(STATUS "natscpp: using system nats.c (shared)")
  elseif(TARGET cnats::nats_static)
    target_link_libraries(natscpp INTERFACE cnats::nats_static)
    set(_natscpp_cnats_target cnats::nats_static)
    message(STATUS "natscpp: using system nats.c (static)")
  else()
    message(FATAL_ERROR
      "cnats package found but provides neither cnats::nats nor cnats::nats_static.")
  endif()
endif()

if(NATSCPP_LINKS_OPENSSL)
  target_link_libraries(natscpp INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()

# A system nats.c does not say whether it has TLS: probe for a TLS-only symbol
# (nats.c built without TLS has no natsOptions_SetCipherSuites at all).
if(NOT _natscpp_use_bundled)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LIBRARIES ${_natscpp_cnats_target})
  if(NATSCPP_LINKS_OPENSSL)
    list(APPEND CMAKE_REQUIRED_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
  endif()
  set(CMAKE_REQUIRED_QUIET ON)
  check_cxx_source_compiles([=[
    #include <nats.h>
    int main() { natsOptions* o = nullptr; return natsOptions_SetCipherSuites(o, "") == NATS_OK ? 0 : 1; }
  ]=] NATSCPP_SYSTEM_NATS_HAS_TLS)
  unset(CMAKE_REQUIRED_LIBRARIES)
  unset(CMAKE_REQUIRED_QUIET)
  set(_natscpp_has_tls ${NATSCPP_SYSTEM_NATS_HAS_TLS})
  message(STATUS "natscpp: system nats.c TLS support: ${_natscpp_has_tls}")
endif()

# Lets the headers call TLS-only nats.c functions (see connection.hpp).
if(_natscpp_has_tls)
  target_compile_definitions(natscpp INTERFACE NATS_HAS_TLS)
endif()

# ── Tests ───────────────────────────────────────────────────────────────────

# Warnings for natscpp's own targets (the headers are compiled as part of these).
set(_natscpp_warnings)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(_natscpp_warnings -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                        -Woverloaded-virtual -Wimplicit-fallthrough
                        -Wno-missing-field-initializers)  # designated initializers leave the rest defaulted
  # Not -Wnull-dereference: its -O2 analysis changes between GCC versions and reports false positives
  # (GCC 13 flags indexing a string_view after checking its size). ASan/UBSan catch real null dereferences.
  if(NATSCPP_WARNINGS_AS_ERRORS)
    list(APPEND _natscpp_warnings -Werror)
  endif()
elseif(MSVC)
  set(_natscpp_warnings /W4 /permissive-)
  if(NATSCPP_WARNINGS_AS_ERRORS)
    list(APPEND _natscpp_warnings /WX)
  endif()
endif()

if(NATSCPP_BUILD_TESTS)
  enable_testing()
  add_executable(natscpp_unit_tests
    tests/unit_tests_main.cpp
    tests/unit_tests_core.cpp
    tests/unit_tests_api_surface.cpp
    tests/unit_tests_integration.cpp
    tests/unit_tests_server_features.cpp
    tests/unit_tests_tls_auth.cpp
    tests/unit_tests_regressions.cpp
    tests/unit_tests_security.cpp
  )
  target_link_libraries(natscpp_unit_tests PRIVATE natscpp::natscpp)
  # Checked against natscpp::version() (the project version is read from version.hpp).
  target_compile_definitions(natscpp_unit_tests PRIVATE NATSCPP_TEST_PROJECT_VERSION="${PROJECT_VERSION}")
  target_compile_options(natscpp_unit_tests PRIVATE ${_natscpp_warnings})
  if(OpenSSL_FOUND)
    target_link_libraries(natscpp_unit_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)  # TLS test callback
  endif()
  # One CTest test per test function, named <group>.<name> and labelled with its group (unit, integration,
  # tls); `ctest -L unit` runs only the tests that need no server. The list comes from the built binary
  # (natscpp_unit_tests --list), like gtest_discover_tests. Server tests report "skipped" when their server
  # is unreachable, or fail with NATSCPP_REQUIRE_SERVER set (tests/ci/start-servers.sh starts the servers).
  set(_natscpp_ctest_file "${CMAKE_CURRENT_BINARY_DIR}/natscpp_unit_tests_list.cmake")
  add_custom_command(TARGET natscpp_unit_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -DEXE=$<TARGET_FILE:natscpp_unit_tests> -DOUT=${_natscpp_ctest_file}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/discover_tests.cmake"
    VERBATIM)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/natscpp_unit_tests_include.cmake"
    "if(EXISTS \"${_natscpp_ctest_file}\")\n"
    "  include(\"${_natscpp_ctest_file}\")\n"
    "else()\n"
    "  add_test(natscpp_unit_tests_NOT_BUILT natscpp_unit_tests_NOT_BUILT)\n"
    "endif()\n")
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/natscpp_unit_tests_include.cmake")
endif()

# ── Examples ────────────────────────────────────────────────────────────────

if(NATSCPP_BUILD_EXAMPLES)
  # Each examples/<name>.cpp builds natscpp_<name>; they expect a server at $NATS_URL (default
  # nats://127.0.0.1:4222).
  set(_natscpp_examples
    sync_example
    async_example
    request_reply_example
    request_reply_server_example
    request_reply_client_example
    headers_trace_example
    queue_group_example
    lifecycle_example
    secure_connection_example
    coroutine_example
    jetstream_sync_pull_example
    jetstream_sync_push_example
    jetstream_async_pull_example
    jetstream_async_push_example
    kv_example)
  foreach(_example IN LISTS _natscpp_examples)
    add_executable(natscpp_${_example} examples/${_example}.cpp)
    target_link_libraries(natscpp_${_example} PRIVATE natscpp::natscpp)
    target_compile_options(natscpp_${_example} PRIVATE ${_natscpp_warnings})
  endforeach()
endif()

# ── Install ─────────────────────────────────────────────────────────────────

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Install only natscpp's own headers.
install(DIRECTORY include/natscpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS natscpp
  EXPORT natscppTargets)

install(EXPORT natscppTargets
  FILE      natscppTargets.cmake
  NAMESPACE natscpp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/natscpp)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/natscppConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/natscppConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/natscpp)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/natscppConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/natscppConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/natscppConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/natscpp)
