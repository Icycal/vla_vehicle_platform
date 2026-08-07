#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$({ cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd; })"

MANIFEST_PATH="${1:?split manifest is required}"
SPLIT_NAME="${2:?split name is required}"
OUTPUT_PATH="${3:?output LeRobot dataset directory is required}"

MANIFEST_PATH="$(realpath "${MANIFEST_PATH}")"
case "${SPLIT_NAME}" in
  train|validation|test) ;;
  *)
    echo "Split name must be train, validation, or test" >&2
    exit 2
    ;;
esac

PATH_FILE="$(mktemp)"
trap 'rm -f "${PATH_FILE}"' EXIT
python3 - "${MANIFEST_PATH}" "${SPLIT_NAME}" "${PATH_FILE}" <<'PY'
import json
import sys
from pathlib import Path

manifest_path = Path(sys.argv[1]).resolve()
split_name = sys.argv[2]
output_path = Path(sys.argv[3])
with manifest_path.open("r", encoding="utf-8") as stream:
    manifest = json.load(stream)
if manifest.get("schema_version") != "vehicle.dataset.split.v1":
    raise ValueError("Unsupported split manifest schema")
entries = manifest.get("splits", {}).get(split_name)
if not entries:
    raise ValueError(f"Split is empty: {split_name}")
with output_path.open("wb") as stream:
    for entry in entries:
        dataset_path = (manifest_path.parent / entry["path"]).resolve()
        stream.write(str(dataset_path).encode("utf-8") + b"\0")
PY

mapfile -d '' -t INPUT_PATHS < "${PATH_FILE}"
"${PROJECT_ROOT}/scripts/convert_lerobot_dataset.sh" \
  "${OUTPUT_PATH}" \
  "${INPUT_PATHS[@]}"