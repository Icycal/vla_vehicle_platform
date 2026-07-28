#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source /opt/ros/humble/setup.bash
source "${PROJECT_ROOT}/ros_ws/install/setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

echo "Observation status:"
timeout 10 ros2 topic echo /vehicle/observation_status --once
echo "Policy observation contract:"
timeout 10 ros2 topic echo /vla/observation --once --field observation_id
timeout 10 ros2 topic echo /vla/observation --once --field schema_version
timeout 10 ros2 topic echo /vla/observation --once --field state_valid
echo "Shadow metrics:"
timeout 10 ros2 topic echo /vla/shadow_metrics --once
echo "Camera rate:"
timeout 12 ros2 topic hz /camera/image_raw --window 10 || true
echo "Compressed camera rate:"
timeout 12 ros2 topic hz /camera/image_compressed --window 10 || true
