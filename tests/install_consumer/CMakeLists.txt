# A downstream project using an installed natscpp: cmake -DCMAKE_PREFIX_PATH=<install-prefix>
cmake_minimum_required(VERSION 3.20)
project(natscpp_install_consumer LANGUAGES CXX)

find_package(natscpp CONFIG REQUIRED)

add_executable(natscpp_install_consumer ../consumer_main.cpp)
target_link_libraries(natscpp_install_consumer PRIVATE natscpp::natscpp)
