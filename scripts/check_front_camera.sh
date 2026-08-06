#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"

echo "Front camera node:"
ros2 node info /front_camera

echo "Raw image topic:"
ros2 topic info /camera/image_raw --verbose

echo "Camera info sample:"
timeout 10 ros2 topic echo /camera/camera_info --once \
  --field header.frame_id

echo "Raw image rate:"
timeout 12 ros2 topic hz /camera/image_raw --window 10 || true

echo "Compressed image rate:"
timeout 12 ros2 topic hz /camera/image_compressed --window 10 || true
