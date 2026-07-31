#!/usr/bin/env bash
set -euo pipefail

ROOT=${VLA_PROJECT_ROOT:-/home/wheeltec/vla_vehicle_platform}
IMAGE=${LEROBOT_IMAGE:-vla-lerobot-compat:0.4.3}
MODEL_ROOT=${SMOLVLA_MODEL_ROOT:-${ROOT}/run/models/smolvla_base}
CACHE_ROOT=${SMOLVLA_CACHE_ROOT:-${ROOT}/run/models/huggingface}

exec docker run --rm \
  --runtime nvidia \
  --network none \
  --ipc host \
  --env HF_HUB_OFFLINE=1 \
  --env HF_HOME=/models/huggingface \
  --env TRANSFORMERS_OFFLINE=1 \
  --env HF_DATASETS_OFFLINE=1 \
  --env NVIDIA_VISIBLE_DEVICES=all \
  --env NVIDIA_DRIVER_CAPABILITIES=compute,utility \
  --volume ${MODEL_ROOT}:/models/smolvla_base:ro \
  --volume ${CACHE_ROOT}:/models/huggingface:ro \
  ${IMAGE} python3 /opt/vla/verify_model_offline.py \
  --model-dir /models/smolvla_base \
  --dependency-manifest /models/huggingface/smolvlm-manifest.json \
  --device cuda
