#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${PROJECT_ROOT}/scripts/lib/runtime_paths.sh"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"
prepare_vla_runtime_paths

echo "Starting Phase 1 Shadow in ROS_DOMAIN_ID=${ROS_DOMAIN_ID}."
echo "The chassis driver is intentionally not launched."
exec ros2 launch vehicle_bringup phase1_shadow.launch.xml \
  policy_socket_path:="${VLA_POLICY_SOCKET}" \
  episode_storage_root:="${VLA_EPISODE_ROOT}" \
  debug_artifact_root:="${VLA_DEBUG_ROOT}" \
  "$@"
