cmake_minimum_required(VERSION 3.8)
project(vehicle_vla_debug)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(rclcpp REQUIRED)
find_package(vehicle_interfaces REQUIRED)
find_package(vehicle_policy_transport REQUIRED)

add_executable(vla_debug_orchestrator src/vla_debug_orchestrator_node.cpp)
ament_target_dependencies(
  vla_debug_orchestrator
  geometry_msgs
  rclcpp
  vehicle_interfaces
  vehicle_policy_transport
)
target_link_libraries(vla_debug_orchestrator nlohmann_json::nlohmann_json)

install(TARGETS vla_debug_orchestrator DESTINATION lib/${PROJECT_NAME})
ament_package()
