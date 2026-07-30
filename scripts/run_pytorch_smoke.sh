#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SMOKE_IMAGE="${PYTORCH_SMOKE_IMAGE:-vla-pytorch-smoke:25.05}"
DURATION_SECONDS="${1:-60}"
LOG_ROOT="${PROJECT_ROOT}/run/test"
SMOKE_LOG="${LOG_ROOT}/cuda-pytorch-smoke-${DURATION_SECONDS}s.log"
TEGRASTATS_LOG="${LOG_ROOT}/tegrastats-pytorch-smoke-${DURATION_SECONDS}s.log"
MOCK_COMPOSE="${PROJECT_ROOT}/policy-runtime/compose.mock.yaml"
mock_was_running=false
tegrastats_pid=""

cleanup() {
  if [[ -n "${tegrastats_pid}" ]]; then
    kill -TERM "${tegrastats_pid}" 2>/dev/null || true
    wait "${tegrastats_pid}" 2>/dev/null || true
  fi
  if [[ "${mock_was_running}" == true ]]; then
    docker compose -f "${MOCK_COMPOSE}" start >/dev/null
  fi
}
trap cleanup EXIT

mkdir -p "${LOG_ROOT}"
if [[ -n "$(docker compose -f "${MOCK_COMPOSE}" ps --status running --quiet policy-runtime)" ]]; then
  mock_was_running=true
  docker compose -f "${MOCK_COMPOSE}" stop
fi

tegrastats --interval 1000 >"${TEGRASTATS_LOG}" 2>&1 &
tegrastats_pid=$!
sleep 2

set +e
docker run --rm \
  --runtime nvidia \
  --network none \
  --shm-size=1g \
  --env NVIDIA_VISIBLE_DEVICES=all \
  --env NVIDIA_DRIVER_CAPABILITIES=compute,utility \
  --env SMOKE_SECONDS="${DURATION_SECONDS}" \
  "${SMOKE_IMAGE}" 2>&1 | tee "${SMOKE_LOG}"
smoke_exit=${PIPESTATUS[0]}
set -e

if [[ "${smoke_exit}" -ne 0 ]]; then
  echo "PyTorch Smoke failed with exit code ${smoke_exit}" >&2
  exit "${smoke_exit}"
fi

grep -q "CUDA_PYTORCH_SMOKE_PASS" "${SMOKE_LOG}"
echo "Smoke log: ${SMOKE_LOG}"
echo "Tegrastats log: ${TEGRASTATS_LOG}"
