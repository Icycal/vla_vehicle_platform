cmake_minimum_required(VERSION 3.8)
project(vehicle_adapters)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(vehicle_interfaces REQUIRED)

add_executable(observation_adapter src/observation_adapter_node.cpp)
ament_target_dependencies(
  observation_adapter
  geometry_msgs
  nav_msgs
  rclcpp
  sensor_msgs
  std_msgs
  vehicle_interfaces
)

install(TARGETS observation_adapter
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
