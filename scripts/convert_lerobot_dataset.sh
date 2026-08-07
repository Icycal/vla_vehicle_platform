#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$({ cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd; })"
IMAGE="${LEROBOT_COMPAT_IMAGE:-vla-lerobot-compat:0.4.3}"
MAPPING_FILE="${LEROBOT_MAPPING_FILE:-${PROJECT_ROOT}/config/lerobot_ackermann_dataset.json}"

OUTPUT_PATH="${1:?output dataset directory is required}"
shift
if [[ $# -eq 0 ]]; then
  echo "At least one vehicle.dataset.v1 directory is required" >&2
  exit 2
fi

OUTPUT_PATH="$(realpath -m "${OUTPUT_PATH}")"
OUTPUT_PARENT="$(dirname "${OUTPUT_PATH}")"
OUTPUT_NAME="$(basename "${OUTPUT_PATH}")"
MAPPING_FILE="$(realpath "${MAPPING_FILE}")"

if [[ -e "${OUTPUT_PATH}" ]]; then
  echo "Output path already exists: ${OUTPUT_PATH}" >&2
  exit 2
fi
mkdir -p "${OUTPUT_PARENT}"

docker_arguments=(
  run --rm
  --network none
  --user "$(id -u):$(id -g)"
  -e HOME=/tmp
  -e HF_HOME=/tmp/huggingface
  -e HF_HUB_OFFLINE=1
  -e TRANSFORMERS_OFFLINE=1
  -e HF_DATASETS_OFFLINE=1
  -v "${PROJECT_ROOT}/policy-runtime/lerobot-compat/convert_vehicle_dataset.py:/opt/vla/convert_vehicle_dataset.py:ro"
  -v "${MAPPING_FILE}:/config/mapping.json:ro"
  -v "${OUTPUT_PARENT}:/output"
)

converter_arguments=(
  python3 /opt/vla/convert_vehicle_dataset.py
  --config /config/mapping.json
  --output "/output/${OUTPUT_NAME}"
)

input_index=0
for input_path in "$@"; do
  input_path="$(realpath "${input_path}")"
  docker_arguments+=( -v "${input_path}:/inputs/${input_index}:ro" )
  converter_arguments+=( --input "/inputs/${input_index}" )
  input_index=$((input_index + 1))
done

docker "${docker_arguments[@]}" "${IMAGE}" "${converter_arguments[@]}"

docker run --rm \
  --network none \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e HF_HOME=/tmp/huggingface \
  -e HF_HUB_OFFLINE=1 \
  -e TRANSFORMERS_OFFLINE=1 \
  -e HF_DATASETS_OFFLINE=1 \
  -v "${PROJECT_ROOT}/policy-runtime/lerobot-compat/verify_vehicle_dataset.py:/opt/vla/verify_vehicle_dataset.py:ro" \
  -v "${OUTPUT_PATH}:/dataset:ro" \
  "${IMAGE}" \
  python3 /opt/vla/verify_vehicle_dataset.py \
    --dataset /dataset
