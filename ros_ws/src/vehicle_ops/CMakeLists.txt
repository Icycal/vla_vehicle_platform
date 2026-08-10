cmake_minimum_required(VERSION 3.8)
project(vehicle_ops)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(vehicle_interfaces REQUIRED)

add_executable(vehicle_ops_api src/vehicle_ops_api_node.cpp)
ament_target_dependencies(
  vehicle_ops_api
  rclcpp
  sensor_msgs
  std_msgs
  vehicle_interfaces
)

install(TARGETS vehicle_ops_api DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch web DESTINATION share/${PROJECT_NAME})

ament_package()