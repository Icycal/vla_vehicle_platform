#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"
"${PROJECT_ROOT}/scripts/prepare_workspace.sh"

cd "${PROJECT_ROOT}/ros_ws"
colcon build \
  --symlink-install \
  --packages-up-to vehicle_bringup \
  --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo
