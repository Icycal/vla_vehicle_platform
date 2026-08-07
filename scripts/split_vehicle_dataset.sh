#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$({ cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd; })"

OUTPUT_PATH="${1:?output split manifest is required}"
shift
if [[ $# -eq 0 ]]; then
  echo "At least one vehicle.dataset.v1 directory is required" >&2
  exit 2
fi

python3 "${PROJECT_ROOT}/tools/dataset/split_vehicle_dataset.py" \
  --output "${OUTPUT_PATH}" \
  "$@"