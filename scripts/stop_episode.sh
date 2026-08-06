#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUCCESS="${1:-false}"
REASON="${2:-operator stop}"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"

ros2 service call /vehicle/stop_episode vehicle_interfaces/srv/StopEpisode \
  "{success: ${SUCCESS}, reason: '${REASON}'}"
