#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 42 "$@"

ros2 node list
ros2 topic list
ros2 topic pub --once /vla/task std_msgs/msg/String "{data: 'phase0 zero-motion check'}"
ros2 service call /vehicle/request_mode vehicle_interfaces/srv/RequestControlMode \
  "{requested_mode: 2, requester: 'phase0_check', reason: 'validate shadow pipeline'}"
timeout 5 ros2 topic echo /cmd_vel --once
