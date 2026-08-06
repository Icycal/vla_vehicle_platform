#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ORIGINAL_ARGUMENTS=("$@")
source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "${ORIGINAL_ARGUMENTS[@]}"

EPISODE_PATH="${1:?episode directory is required}"
OUTPUT_PATH="${2:-${PROJECT_ROOT}/datasets/exports/$(basename "${EPISODE_PATH}")}"
extra_arguments=()
if [[ $# -gt 2 ]]; then
  extra_arguments=("${@:3}")
fi

mkdir -p "$(dirname "${OUTPUT_PATH}")"
exec ros2 run vehicle_data dataset_exporter \
  --episode "${EPISODE_PATH}" \
  --output "${OUTPUT_PATH}" \
  "${extra_arguments[@]}"
