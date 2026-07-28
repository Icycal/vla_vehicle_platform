#!/usr/bin/env bash
set -euo pipefail

RUNTIME_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(cd "${RUNTIME_ROOT}/.." && pwd)"

mkdir -p "${RUNTIME_ROOT}/generated"
touch "${RUNTIME_ROOT}/generated/__init__.py"
protoc \
  --proto_path="${PROJECT_ROOT}/protocol" \
  --python_out="${RUNTIME_ROOT}/generated" \
  "${PROJECT_ROOT}/protocol/policy_protocol.proto"

echo "Generated Python protocol bindings in ${RUNTIME_ROOT}/generated"
