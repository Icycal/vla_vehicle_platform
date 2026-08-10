#!/usr/bin/env bash
set -eo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
source /opt/ros/humble/setup.bash
source "${PROJECT_ROOT}/ros_ws/install/local_setup.bash"
case "${1:-}" in
  front_camera)
    exec ros2 launch vehicle_bringup front_camera.launch.xml
    ;;
  runtime_core)
    exec ros2 launch vehicle_bringup phase0.launch.xml policy_params_file:="${PROJECT_ROOT}/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/policy_smolvla_shadow.yaml"
    ;;
  observation_pipeline)
    exec ros2 launch vehicle_bringup observation_pipeline.launch.xml
    ;;
  vla_debug_pipeline)
    exec ros2 launch vehicle_bringup vla_debug_pipeline.launch.xml policy_params_file:="${PROJECT_ROOT}/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/policy_smolvla_shadow.yaml"
    ;;
  shadow_data)
    exec ros2 launch vehicle_bringup shadow_data.launch.xml
    ;;
  *)
    echo "Unsupported managed component: ${1:-}" >&2
    exit 64
    ;;
esac