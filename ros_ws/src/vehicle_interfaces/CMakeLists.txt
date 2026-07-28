cmake_minimum_required(VERSION 3.8)
project(vehicle_interfaces)

find_package(ament_cmake REQUIRED)
find_package(action_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  msg/ControlLease.msg
  msg/PolicyAction.msg
  msg/PolicyStatus.msg
  msg/SafetyEvent.msg
  msg/SystemState.msg
  msg/TeleopCommand.msg
  srv/RequestControlMode.srv
  srv/RequestSafeStop.srv
  action/AcquireControlLease.action
  DEPENDENCIES action_msgs builtin_interfaces geometry_msgs std_msgs
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
