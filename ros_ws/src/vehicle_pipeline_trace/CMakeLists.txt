cmake_minimum_required(VERSION 3.8)
project(vehicle_pipeline_trace)

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
find_package(sensor_msgs REQUIRED)
find_package(vehicle_interfaces REQUIRED)

add_executable(pipeline_trace_aggregator src/pipeline_trace_aggregator_node.cpp)
ament_target_dependencies(
  pipeline_trace_aggregator
  geometry_msgs
  rclcpp
  sensor_msgs
  vehicle_interfaces
)
target_link_libraries(pipeline_trace_aggregator nlohmann_json::nlohmann_json)

install(TARGETS pipeline_trace_aggregator DESTINATION lib/${PROJECT_NAME})

ament_package()