#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUCCESS="${1:-false}"
REASON="${2:-operator stop}"

source /opt/ros/humble/setup.bash
source "${PROJECT_ROOT}/ros_ws/install/setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

ros2 service call /vehicle/stop_episode vehicle_interfaces/srv/StopEpisode \
  "{success: ${SUCCESS}, reason: '${REASON}'}"
