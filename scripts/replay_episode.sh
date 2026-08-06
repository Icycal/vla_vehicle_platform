#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ORIGINAL_ARGUMENTS=("$@")
source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "${ORIGINAL_ARGUMENTS[@]}"

EPISODE_PATH="${1:?episode directory is required}"
PLAYBACK_RATE="${2:-1.0}"
MAX_MESSAGES="${3:-0}"

exec ros2 run vehicle_data observation_replay --ros-args \
  -p bag_path:="${EPISODE_PATH}" \
  -p output_topic:=/vla/replay/observation \
  -p playback_rate:="${PLAYBACK_RATE}" \
  -p validity_seconds:=5.0 \
  -p max_messages:="${MAX_MESSAGES}"
