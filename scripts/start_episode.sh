#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EPISODE_ID="${1:-}"
TASK="${2:-camera shadow capture}"
OPERATOR_ID="${3:-wheeltec}"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"

ros2 service call /vehicle/start_episode vehicle_interfaces/srv/StartEpisode \
  "{episode_id: '${EPISODE_ID}', task: '${TASK}', operator_id: '${OPERATOR_ID}'}"
