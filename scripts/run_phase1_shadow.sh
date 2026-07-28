#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source /opt/ros/humble/setup.bash
source "${PROJECT_ROOT}/ros_ws/install/setup.bash"

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

echo "Starting Phase 1 Shadow in ROS_DOMAIN_ID=${ROS_DOMAIN_ID}."
echo "The chassis driver is intentionally not launched."
exec ros2 launch vehicle_bringup phase1_shadow.launch.xml "$@"
