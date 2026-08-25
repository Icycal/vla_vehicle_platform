#!/usr/bin/env bash
set -euo pipefail

ROOT=${VLA_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
IMAGE=${LEROBOT_IMAGE:-vla-lerobot-compat:0.4.3}
MODEL_ID=${SMOLVLA_MODEL_ID:-lerobot/smolvla_base}
REVISION=${SMOLVLA_MODEL_REVISION:-main}
WORKERS=${SMOLVLA_DOWNLOAD_WORKERS:-2}
MODEL_ROOT=${SMOLVLA_MODEL_ROOT:-${ROOT}/run/models/smolvla_base}
GROUP_ID=${GROUPS[0]}

mkdir -p ${MODEL_ROOT} ${ROOT}/run/test

exec docker run --rm --network host \
  --user ${UID}:${GROUP_ID} \
  --env HOME=/tmp \
  --env HF_HOME=/tmp/huggingface \
  --env HF_ENDPOINT \
  --env HF_TOKEN \
  --env HF_HUB_ETAG_TIMEOUT=${HF_HUB_ETAG_TIMEOUT:-120} \
  --env HF_HUB_DOWNLOAD_TIMEOUT=${HF_HUB_DOWNLOAD_TIMEOUT:-300} \
  --volume ${MODEL_ROOT}:/models/smolvla_base \
  ${IMAGE} python3 /opt/vla/download_model.py \
  --repo-id ${MODEL_ID} \
  --revision ${REVISION} \
  --output-dir /models/smolvla_base \
  --max-workers ${WORKERS}
