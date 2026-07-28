#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EPISODE_ID="${1:-}"
TASK="${2:-camera shadow capture}"
OPERATOR_ID="${3:-wheeltec}"

source /opt/ros/humble/setup.bash
source "${PROJECT_ROOT}/ros_ws/install/setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

ros2 service call /vehicle/start_episode vehicle_interfaces/srv/StartEpisode \
  "{episode_id: '${EPISODE_ID}', task: '${TASK}', operator_id: '${OPERATOR_ID}'}"
