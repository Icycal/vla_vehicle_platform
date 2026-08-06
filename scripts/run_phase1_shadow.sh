#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"

echo "Starting Phase 1 Shadow in ROS_DOMAIN_ID=${ROS_DOMAIN_ID}."
echo "The chassis driver is intentionally not launched."
exec ros2 launch vehicle_bringup phase1_shadow.launch.xml "$@"
