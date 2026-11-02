# vipee_bridge: ROS 2 process of the bridge. Plain CMake, no colcon, so the
# same file serves the NDK cross-build (build-bridge.sh) and the host test
# build in the Jazzy container (test-ros.sh, -DVIPEE_ROS_TESTS=ON).
cmake_minimum_required(VERSION 3.22)
project(vipee_bridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(rclcpp REQUIRED)
find_package(sirv_msgs REQUIRED)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
add_library(vipee_bridge_lib STATIC
  ${CORE}/src/vehicle_map.cpp ${CORE}/src/translate.cpp ${CORE}/src/bridge.cpp
  src/socket_sink.cpp src/bridge_node.cpp)
target_include_directories(vipee_bridge_lib PUBLIC ${CORE}/include include)
target_link_libraries(vipee_bridge_lib PUBLIC rclcpp::rclcpp
  ${sirv_msgs_TARGETS})

add_executable(vipee_bridge src/main.cpp)
target_link_libraries(vipee_bridge vipee_bridge_lib)
if(ANDROID)
  target_link_libraries(vipee_bridge log)
endif()

option(VIPEE_ROS_TESTS "Build the ROS integration test" OFF)
if(VIPEE_ROS_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_executable(bridge_node_rostest test/bridge_node_rostest.cpp)
  target_compile_definitions(bridge_node_rostest PRIVATE
    VIPEE_MAPS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../maps")
  target_link_libraries(bridge_node_rostest vipee_bridge_lib GTest::gtest_main)
  add_test(NAME bridge_node_rostest COMMAND bridge_node_rostest)
  # CTS-23: an unreadable map file is refused with MAP_INVALID and the process
  # stays up (timeout kills it: exit 124), instead of an init crash loop.
  add_test(NAME unreadable_map_test COMMAND sh -c
    "VIPEE_VHAL_SOCKET=/tmp/vipee_no_vhal timeout 3 $<TARGET_FILE:vipee_bridge> --ros-args --params-file ${CMAKE_CURRENT_SOURCE_DIR}/test/unreadable_map.yaml 2> unreadable_map.log; rc=$?; cat unreadable_map.log; [ $rc -eq 124 ] && [ $(grep -c 'UNAVAILABLE MAP_INVALID' unreadable_map.log) -eq 4 ]")
endif()
