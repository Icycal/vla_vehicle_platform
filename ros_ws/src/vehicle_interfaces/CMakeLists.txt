cmake_minimum_required(VERSION 3.8)
project(vehicle_interfaces)

find_package(ament_cmake REQUIRED)
find_package(action_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  msg/ComponentState.msg
  msg/ControlLease.msg
  msg/EpisodeState.msg
  msg/ObservationStatus.msg
  msg/PolicyAction.msg
  msg/PolicyObservation.msg
  msg/PolicyStatus.msg
  msg/PipelineStage.msg
  msg/PipelineTrace.msg
  msg/SafetyEvent.msg
  msg/ShadowComparison.msg
  msg/ShadowMetrics.msg
  msg/SystemState.msg
  msg/TeleopCommand.msg
  srv/ControlComponent.srv
  srv/ControlProfile.srv
  srv/ReadComponentLog.srv
  srv/ListComponents.srv
  srv/CaptureVlaDebug.srv
  srv/RunVlaDebug.srv
  srv/RequestControlMode.srv
  srv/RequestSafeStop.srv
  srv/StartEpisode.srv
  srv/StopEpisode.srv
  action/AcquireControlLease.action
  DEPENDENCIES action_msgs builtin_interfaces geometry_msgs sensor_msgs std_msgs
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
