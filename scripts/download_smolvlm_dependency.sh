#!/usr/bin/env bash
set -euo pipefail

ROOT=${VLA_PROJECT_ROOT:-/home/wheeltec/vla_vehicle_platform}
IMAGE=${LEROBOT_IMAGE:-vla-lerobot-compat:0.4.3}
REPO_ID=HuggingFaceTB/SmolVLM2-500M-Video-Instruct
REVISION=${SMOLVLM_REVISION:-main}
CACHE_ROOT=${SMOLVLA_CACHE_ROOT:-${ROOT}/run/models/huggingface}
GROUP_ID=${GROUPS[0]}

mkdir -p ${CACHE_ROOT}
exec docker run --rm --network host \
  --user ${UID}:${GROUP_ID} \
  --env HOME=/tmp \
  --env HF_ENDPOINT \
  --env HF_TOKEN \
  --env HF_HUB_ETAG_TIMEOUT=${HF_HUB_ETAG_TIMEOUT:-120} \
  --env HF_HUB_DOWNLOAD_TIMEOUT=${HF_HUB_DOWNLOAD_TIMEOUT:-300} \
  --volume ${CACHE_ROOT}:/models/huggingface \
  ${IMAGE} python3 /opt/vla/download_dependency.py \
  --repo-id ${REPO_ID} \
  --revision ${REVISION} \
  --cache-dir /models/huggingface/hub \
  --manifest /models/huggingface/smolvlm-manifest.json \
  --max-workers ${SMOLVLA_DOWNLOAD_WORKERS:-2}
