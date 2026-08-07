#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$({ cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd; })"
IMAGE="${SMOLVLA_TRAINING_IMAGE:-vla-smolvla-training:0.4.3}"
BASE_IMAGE="${SMOLVLA_TRAINING_BASE_IMAGE:-pytorch/pytorch:2.7.1-cuda12.8-cudnn9-runtime}"
PIP_INDEX_URL="${PIP_INDEX_URL:-https://pypi.org/simple}"

if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "The full training image is intended for an x86_64 CUDA workstation." >&2
  exit 2
fi

docker build \
  --file "${PROJECT_ROOT}/training/Dockerfile.gpu" \
  --build-arg "BASE_IMAGE=${BASE_IMAGE}" \
  --build-arg "LEROBOT_VERSION=0.4.3" \
  --build-arg "PIP_INDEX_URL=${PIP_INDEX_URL}" \
  --tag "${IMAGE}" \
  "${PROJECT_ROOT}"