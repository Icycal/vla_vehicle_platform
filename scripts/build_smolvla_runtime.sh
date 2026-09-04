#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE_IMAGE="${SMOLVLA_BASE_IMAGE:-vla-lerobot-compat:0.4.3}"
RUNTIME_IMAGE="${SMOLVLA_RUNTIME_IMAGE:-vla-smolvla-runtime:0.1}"

cd "${PROJECT_ROOT}"
"${PROJECT_ROOT}/policy-runtime/scripts/generate_protocol.sh"

docker build \
  --pull=false \
  --network=host \
  --build-arg BASE_IMAGE="${BASE_IMAGE}" \
  --build-arg INSTALL_TORCHAO="${SMOLVLA_INSTALL_TORCHAO:-0}" \
  --build-arg TORCHAO_VERSION="${SMOLVLA_TORCHAO_VERSION:-}" \
  --file policy-runtime/Dockerfile.smolvla-runtime \
  --tag "${RUNTIME_IMAGE}" \
  .

echo "Built ${RUNTIME_IMAGE}"
