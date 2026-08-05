#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(
  cd "$(dirname "${BASH_SOURCE[0]}")/.."
  pwd
)"

ENV_FILE="${PROJECT_ROOT}/run/config/smolvla-demo.env"
LOG_FILE="${PROJECT_ROOT}/run/test/smolvla-demo/logs/image-inference.log"

cd "${PROJECT_ROOT}"

mkdir -p "$(dirname "${LOG_FILE}")"

docker compose \
  -p smolvla-demo \
  --env-file "${ENV_FILE}" \
  -f policy-runtime/compose.smolvla-demo.yaml \
  run \
  --rm \
  --no-deps \
  smolvla-demo \
  2>&1 | tee "${LOG_FILE}"