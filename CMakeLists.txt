cmake_minimum_required(VERSION 3.21)
project(vipee-ros2aaosbridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

find_package(GTest CONFIG REQUIRED)

enable_testing()
include(GoogleTest)

# Bridge core: C++17 stdlib only, no ROS and no Android.
add_library(vipee_core core/src/vehicle_map.cpp core/src/translate.cpp
  core/src/bridge.cpp)
target_include_directories(vipee_core PUBLIC ${CMAKE_SOURCE_DIR}/core/include)

# Bridge sink: POSIX only, no ROS, so the unit test builds on the host.
add_library(vipee_socket_sink ros/vipee_bridge/src/socket_sink.cpp)
target_include_directories(vipee_socket_sink PUBLIC
  ${CMAKE_SOURCE_DIR}/ros/vipee_bridge/include)
target_link_libraries(vipee_socket_sink PUBLIC vipee_core)

function(add_unit_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE GTest::gtest_main vipee_core ${ARGN})
  gtest_discover_tests(${name} PROPERTIES LABELS unit)
endfunction()

add_unit_test(smoke_test core/test/smoke_test.cpp)
add_unit_test(wire_test core/test/wire_test.cpp)
add_unit_test(vehicle_map_test core/test/vehicle_map_test.cpp)
add_unit_test(translate_test core/test/translate_test.cpp)
add_unit_test(bridge_test core/test/bridge_test.cpp)
add_unit_test(socket_sink_test ros/vipee_bridge/test/socket_sink_test.cpp
  vipee_socket_sink)

# VHAL ingest logic: header-only, no AOSP, unit-tested on the host.
add_unit_test(vhal_ingest_test android/vhal/test/vhal_ingest_test.cpp)
target_include_directories(vhal_ingest_test PRIVATE
  ${CMAKE_SOURCE_DIR}/android/vhal/include)
