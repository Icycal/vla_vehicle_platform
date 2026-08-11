cmake_minimum_required(VERSION 3.8)
project(vehicle_storage)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(vehicle_interfaces REQUIRED)

add_executable(storage_manager src/storage_manager_node.cpp)
ament_target_dependencies(storage_manager rclcpp vehicle_interfaces)

install(TARGETS storage_manager DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

ament_package()
