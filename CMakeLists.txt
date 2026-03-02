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

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(NATS QUIET IMPORTED_TARGET libnats)
endif()

if(TARGET PkgConfig::NATS)
  target_link_libraries(natscpp INTERFACE PkgConfig::NATS)
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
