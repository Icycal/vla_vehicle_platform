#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE_IMAGE="${PYTORCH_BASE_IMAGE:-vla-pytorch-base:25.05-igpu}"
SMOKE_IMAGE="${PYTORCH_SMOKE_IMAGE:-vla-pytorch-smoke:25.05}"

architecture="$(docker image inspect "${BASE_IMAGE}" --format '{{.Architecture}}')"
if [[ "${architecture}" != "arm64" ]]; then
  echo "Expected ARM64 base image, got ${architecture}" >&2
  exit 1
fi

docker build \
  --pull=false \
  --network=host \
  --build-arg BASE_IMAGE="${BASE_IMAGE}" \
  --file "${PROJECT_ROOT}/policy-runtime/Dockerfile.pytorch-smoke" \
  --tag "${SMOKE_IMAGE}" \
  "${PROJECT_ROOT}"

echo "Built ${SMOKE_IMAGE} from ${BASE_IMAGE}"
