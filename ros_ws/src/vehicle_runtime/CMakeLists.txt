cmake_minimum_required(VERSION 3.8)
project(vehicle_runtime)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(vehicle_interfaces REQUIRED)
find_package(vehicle_policy_transport REQUIRED)

add_executable(vehicle_supervisor src/vehicle_supervisor_node.cpp)
ament_target_dependencies(vehicle_supervisor rclcpp vehicle_interfaces)

add_executable(vla_policy_gateway src/vla_policy_gateway_node.cpp)
ament_target_dependencies(
  vla_policy_gateway geometry_msgs rclcpp vehicle_interfaces vehicle_policy_transport)

add_executable(vla_action_runtime src/vla_action_runtime_node.cpp)
ament_target_dependencies(vla_action_runtime geometry_msgs rclcpp vehicle_interfaces)

add_executable(vehicle_action_capture src/vehicle_action_capture_node.cpp)
ament_target_dependencies(vehicle_action_capture geometry_msgs rclcpp vehicle_interfaces)

add_executable(vla_control_mux src/vla_control_mux_node.cpp)
ament_target_dependencies(vla_control_mux geometry_msgs rclcpp vehicle_interfaces)

add_executable(vla_safety_guard src/vla_safety_guard_node.cpp)
ament_target_dependencies(vla_safety_guard geometry_msgs rclcpp sensor_msgs vehicle_interfaces)

install(TARGETS
  vehicle_supervisor
  vehicle_action_capture
  vla_policy_gateway
  vla_action_runtime
  vla_control_mux
  vla_safety_guard
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/ DESTINATION include)

ament_package()
