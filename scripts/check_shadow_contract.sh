#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TASK="${1:-shadow contract validation}"

source /opt/ros/humble/setup.bash
source "${PROJECT_ROOT}/ros_ws/install/setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

ros2 topic pub --once /vla/task std_msgs/msg/String "{data: '${TASK}'}"
ros2 service call /vehicle/request_mode vehicle_interfaces/srv/RequestControlMode \
  "{requested_mode: 2, requester: 'shadow_contract_check', reason: 'validate observation correlation'}"

echo "Observation ID:"
timeout 10 ros2 topic echo /vla/observation --once --field observation_id
echo "Observation schema:"
timeout 10 ros2 topic echo /vla/observation --once --field schema_version
echo "State validity:"
timeout 10 ros2 topic echo /vla/observation --once --field state_valid
echo "Policy observation correlation:"
timeout 10 ros2 topic echo /vla/policy_action --once --field observation_id
echo "Shadow comparison:"
timeout 10 ros2 topic echo /vla/shadow_comparison --once
echo "Shadow metrics:"
timeout 10 ros2 topic echo /vla/shadow_metrics --once
echo "Final command:"
timeout 10 ros2 topic echo /cmd_vel --once
