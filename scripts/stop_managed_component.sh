#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${PROJECT_ROOT}/scripts/lib/runtime_paths.sh"
prepare_vla_runtime_paths
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
source "${ROS_SETUP_FILE:-/opt/ros/${ROS_DISTRO:-humble}/setup.bash}"
source "${PROJECT_ROOT}/ros_ws/install/local_setup.bash"

case "${1:-}" in
  vehicle_chassis)
    config_file="${VLA_CHASSIS_ENV_FILE:-${VLA_RUNTIME_ROOT}/config/chassis.env}"
    if [[ -f "${config_file}" ]]; then
      set -a
      source "${config_file}"
      set +a
    fi
    command_topic="${VLA_CHASSIS_COMMAND_TOPIC:-/cmd_vel}"
    timeout 3 ros2 topic pub --once "${command_topic}" geometry_msgs/msg/Twist \
      '{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}' >/dev/null 2>&1 || true
    ;;
  *)
    exit 0
    ;;
esac