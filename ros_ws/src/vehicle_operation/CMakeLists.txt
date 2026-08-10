cmake_minimum_required(VERSION 3.8)
project(vehicle_operation)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(vehicle_interfaces REQUIRED)

add_executable(operation_orchestrator src/operation_orchestrator_node.cpp)
ament_target_dependencies(operation_orchestrator rclcpp vehicle_interfaces)

install(TARGETS operation_orchestrator DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
ament_package()