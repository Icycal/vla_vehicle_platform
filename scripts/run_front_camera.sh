#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAMERA_DEVICE="/dev/video0"

if [[ ! -e "${CAMERA_DEVICE}" ]]; then
  echo "Camera device not found: ${CAMERA_DEVICE}" >&2
  exit 1
fi

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"

echo "Starting front camera from ${CAMERA_DEVICE} in ROS_DOMAIN_ID=${ROS_DOMAIN_ID}."
exec ros2 launch vehicle_bringup front_camera.launch.xml "$@"
