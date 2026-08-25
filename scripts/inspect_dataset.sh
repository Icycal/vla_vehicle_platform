#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$({ cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd; })"
IMAGE="${LEROBOT_COMPAT_IMAGE:-vla-lerobot-compat:0.4.3}"

DATASET_PATH="${1:?dataset directory is required}"
OUTPUT_PATH="${2:?output report directory is required}"
shift 2

DATASET_PATH="$(realpath "${DATASET_PATH}")"
OUTPUT_PATH="$(realpath -m "${OUTPUT_PATH}")"
OUTPUT_PARENT="$(dirname "${OUTPUT_PATH}")"
OUTPUT_NAME="$(basename "${OUTPUT_PATH}")"

if [[ -e "${OUTPUT_PATH}" ]]; then
  echo "Output path already exists: ${OUTPUT_PATH}" >&2
  exit 2
fi
mkdir -p "${OUTPUT_PARENT}"

docker run --rm \
  --network none \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e HF_HOME=/tmp/huggingface \
  -e HF_HUB_OFFLINE=1 \
  -e TRANSFORMERS_OFFLINE=1 \
  -e HF_DATASETS_OFFLINE=1 \
  -v "${PROJECT_ROOT}/tools/dataset/inspect_dataset.py:/opt/vla/inspect_dataset.py:ro" \
  -v "${DATASET_PATH}:/dataset:ro" \
  -v "${OUTPUT_PARENT}:/output" \
  "${IMAGE}" \
  python3 /opt/vla/inspect_dataset.py \
    --dataset /dataset \
    --output "/output/${OUTPUT_NAME}" \
    "$@"
python3 - "${OUTPUT_PATH}/vehicle_ops_source.json" "${DATASET_PATH}" <<'PY'
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
Path(sys.argv[1]).write_text(json.dumps({
    "schema_version": "vehicle.ops.quality-source.v1",
    "dataset_path": sys.argv[2],
    "generated_at": datetime.now(timezone.utc).isoformat(),
}, indent=2) + "\n", encoding="utf-8")
PY
