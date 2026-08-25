#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${PROJECT_ROOT}/scripts/lib/runtime_paths.sh"
RUNTIME_ROOT="${PROJECT_ROOT}/policy-runtime"
SOCKET_PATH="${POLICY_SOCKET_PATH:-${VLA_POLICY_SOCKET}}"

mkdir -p "$(dirname "${SOCKET_PATH}")"
"${RUNTIME_ROOT}/scripts/generate_protocol.sh"

export PYTHONPATH="${RUNTIME_ROOT}:${PYTHONPATH:-}"
export POLICY_PROVIDER="${POLICY_PROVIDER:-mock}"
export POLICY_SOCKET_PATH="${SOCKET_PATH}"
export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION="${PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION:-python}"

echo "Starting mock Policy Runtime at ${POLICY_SOCKET_PATH}"
exec python3 -m runtime.server
