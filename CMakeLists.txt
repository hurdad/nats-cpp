cmake_minimum_required(VERSION 3.20)

project(natscpp
  VERSION 0.1.2
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

# nats.c backend selection:
#   auto    — use a system-installed cnats if find_package succeeds; otherwise
#             fall back to the bundled third_party/nats.c submodule (default)
#   system  — require a system-installed cnats; error if not found
#   bundled — always use the bundled submodule; error if submodule is absent
set(NATSCPP_NATS_BACKEND "auto" CACHE STRING
  "nats.c backend: auto | system | bundled")
set_property(CACHE NATSCPP_NATS_BACKEND PROPERTY STRINGS auto system bundled)

include(CTest)

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
else()  # auto: prefer system, fall back to bundled
  find_package(cnats QUIET)
  if(cnats_FOUND)
    set(_natscpp_use_bundled OFF)
    message(STATUS "natscpp: system cnats found — using system nats.c")
  else()
    set(_natscpp_use_bundled ON)
    message(STATUS "natscpp: system cnats not found — falling back to bundled nats.c")
  endif()
endif()

if(_natscpp_use_bundled)
  # ── Bundled submodule (static) ───────────────────────────────────────────

  # Save the user's BUILD_TESTING preference before nats.c forces it off.
  set(_natscpp_build_testing "${BUILD_TESTING}")

  set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_LIB_STATIC ON  CACHE BOOL "" FORCE)
  set(NATS_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_STREAMING  OFF CACHE BOOL "" FORCE)
  if(NATSCPP_BUNDLED_WITH_TLS AND NOT OpenSSL_FOUND)
    message(FATAL_ERROR "NATSCPP_BUNDLED_WITH_TLS=ON but OpenSSL was not found")
  endif()
  set(NATS_BUILD_WITH_TLS ${NATSCPP_BUNDLED_WITH_TLS} CACHE BOOL "" FORCE)
  set(NATSCPP_LINKS_OPENSSL ${NATSCPP_BUNDLED_WITH_TLS})
  set(_natscpp_has_tls ${NATSCPP_BUNDLED_WITH_TLS})
  message(STATUS "natscpp: bundled nats.c TLS support: ${NATSCPP_BUNDLED_WITH_TLS}")
  set(NATS_BUILD_WITH_DEPRECATED OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)

  add_subdirectory("${NATS_C_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/nats.c")
  target_link_libraries(natscpp INTERFACE nats_static)
  set_target_properties(nats_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

  # In the build tree the include/nats/nats.h shim uses #include_next to reach
  # nats.c's real headers; expose the nats.c src dir so that works.
  target_include_directories(natscpp
    INTERFACE $<BUILD_INTERFACE:${NATS_C_SOURCE_DIR}/src>)

  # Restore the user's preference now that the nats.c subdir is done.
  set(BUILD_TESTING "${_natscpp_build_testing}" CACHE BOOL "" FORCE)

else()
  # ── System nats.c via its cmake package ─────────────────────────────────
  # nats.c installs a cmake package named "cnats" for both shared and static
  # builds.  Prefer the shared target (cnats::nats); fall back to static.
  find_package(cnats REQUIRED)
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

if(BUILD_TESTING)
  add_executable(natscpp_unit_tests
    tests/unit_tests_main.cpp
    tests/unit_tests_core.cpp
    tests/unit_tests_api_surface.cpp
    tests/unit_tests_integration.cpp
  )
  target_link_libraries(natscpp_unit_tests PRIVATE natscpp::natscpp)
  add_test(NAME natscpp_unit_tests COMMAND natscpp_unit_tests)
endif()

# ── Examples ────────────────────────────────────────────────────────────────

option(NATSCPP_BUILD_EXAMPLES "Build natscpp examples" ON)

if(NATSCPP_BUILD_EXAMPLES)
  add_executable(natscpp_sync_example examples/sync_example.cpp)
  target_link_libraries(natscpp_sync_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_async_example examples/async_example.cpp)
  target_link_libraries(natscpp_async_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_jetstream_sync_pull_example examples/jetstream_sync_pull_example.cpp)
  target_link_libraries(natscpp_jetstream_sync_pull_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_jetstream_sync_push_example examples/jetstream_sync_push_example.cpp)
  target_link_libraries(natscpp_jetstream_sync_push_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_jetstream_async_pull_example examples/jetstream_async_pull_example.cpp)
  target_link_libraries(natscpp_jetstream_async_pull_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_jetstream_async_push_example examples/jetstream_async_push_example.cpp)
  target_link_libraries(natscpp_jetstream_async_push_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_kv_example examples/kv_example.cpp)
  target_link_libraries(natscpp_kv_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_request_reply_example examples/request_reply_example.cpp)
  target_link_libraries(natscpp_request_reply_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_request_reply_server_example examples/request_reply_server_example.cpp)
  target_link_libraries(natscpp_request_reply_server_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_request_reply_client_example examples/request_reply_client_example.cpp)
  target_link_libraries(natscpp_request_reply_client_example PRIVATE natscpp::natscpp)
endif()

# ── Install ─────────────────────────────────────────────────────────────────

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Install only natscpp's own headers.  The include/nats/nats.h shim is a
# build-tree-only convenience; installing it would shadow nats.c's real header.
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
