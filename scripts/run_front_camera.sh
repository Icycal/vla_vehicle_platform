#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAMERA_DEVICE="/dev/video0"

if [[ ! -e "${CAMERA_DEVICE}" ]]; then
  echo "Camera device not found: ${CAMERA_DEVICE}" >&2
  exit 1
fi

source /opt/ros/humble/setup.bash
source "${PROJECT_ROOT}/ros_ws/install/setup.bash"

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

echo "Starting front camera from ${CAMERA_DEVICE} in ROS_DOMAIN_ID=${ROS_DOMAIN_ID}."
exec ros2 launch vehicle_bringup front_camera.launch.xml "$@"
