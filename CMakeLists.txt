cmake_minimum_required(VERSION 3.20)

project(natscpp
  VERSION 0.1.0
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

include(CTest)

if(EXISTS "${NATS_C_SOURCE_DIR}/CMakeLists.txt")
  # Save the user's BUILD_TESTING preference before nats.c forces it off.
  set(_natscpp_build_testing "${BUILD_TESTING}")

  set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_LIB_STATIC ON CACHE BOOL "" FORCE)
  set(NATS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_STREAMING OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_WITH_TLS OFF CACHE BOOL "" FORCE)
  set(NATS_BUILD_WITH_DEPRECATED OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

  add_subdirectory("${NATS_C_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/nats.c")
  target_link_libraries(natscpp INTERFACE nats_static)
  target_include_directories(natscpp
    INTERFACE
      $<BUILD_INTERFACE:${NATS_C_SOURCE_DIR}/src>)

  # Restore the user's preference now that the nats.c subdir is done.
  set(BUILD_TESTING "${_natscpp_build_testing}" CACHE BOOL "" FORCE)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(NATS QUIET IMPORTED_TARGET libnats)
  endif()

  if(TARGET PkgConfig::NATS)
    target_link_libraries(natscpp INTERFACE PkgConfig::NATS)
  else()
    message(FATAL_ERROR
      "Unable to find libnats dependency. Initialize third_party/nats.c submodule "
      "or install system libnats (pkg-config package: libnats).")
  endif()
endif()

if(BUILD_TESTING)
  add_executable(natscpp_unit_tests tests/unit_tests.cpp)
  target_link_libraries(natscpp_unit_tests PRIVATE natscpp::natscpp)
  add_test(NAME natscpp_unit_tests COMMAND natscpp_unit_tests)
endif()

option(NATSCPP_BUILD_EXAMPLES "Build natscpp examples" ON)

if(NATSCPP_BUILD_EXAMPLES)
  add_executable(natscpp_sync_example examples/sync_example.cpp)
  target_link_libraries(natscpp_sync_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_async_example examples/async_example.cpp)
  target_link_libraries(natscpp_async_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_jetstream_sync_example examples/jetstream_sync_example.cpp)
  target_link_libraries(natscpp_jetstream_sync_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_jetstream_async_example examples/jetstream_async_example.cpp)
  target_link_libraries(natscpp_jetstream_async_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_kv_example examples/kv_example.cpp)
  target_link_libraries(natscpp_kv_example PRIVATE natscpp::natscpp)

  add_executable(natscpp_request_reply_example examples/request_reply_example.cpp)
  target_link_libraries(natscpp_request_reply_example PRIVATE natscpp::natscpp)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS natscpp
  EXPORT natscppTargets)

install(EXPORT natscppTargets
  FILE natscppTargets.cmake
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
