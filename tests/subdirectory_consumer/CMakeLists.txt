# A downstream project embedding natscpp with add_subdirectory. natscpp's own tests and examples must not
# become part of this build.
cmake_minimum_required(VERSION 3.20)
project(natscpp_subdirectory_consumer LANGUAGES CXX)

include(CTest)  # the parent's BUILD_TESTING is ON; natscpp must still not add its tests
add_subdirectory(../.. natscpp)

add_executable(natscpp_subdirectory_consumer ../consumer_main.cpp)
target_link_libraries(natscpp_subdirectory_consumer PRIVATE natscpp::natscpp)
