#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODEL_DIR="${1:?model directory is required}"
DATASET_DIR="${2:?LeRobot dataset directory is required}"
exec python3 "${PROJECT_ROOT}/tools/model/write_action_manifest.py" \
  --model-dir "${MODEL_DIR}" \
  --dataset "${DATASET_DIR}"
