cmake_minimum_required(VERSION 3.22)
project(vipee_bringup CXX)

find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)

add_executable(twist_listener twist_listener.cpp)
target_link_libraries(twist_listener rclcpp::rclcpp ${geometry_msgs_TARGETS} log)
