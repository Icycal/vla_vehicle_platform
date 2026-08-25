#!/usr/bin/env bash

if [[ -z "${PROJECT_ROOT:-}" ]]; then
  PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fi

export VLA_HOME="${VLA_HOME:-${PROJECT_ROOT}}"
export VLA_STATE_ROOT="${VLA_STATE_ROOT:-${PROJECT_ROOT}}"
export VLA_RUNTIME_ROOT="${VLA_RUNTIME_ROOT:-${PROJECT_ROOT}/run}"
export VLA_LOG_ROOT="${VLA_LOG_ROOT:-${VLA_RUNTIME_ROOT}/log}"
export VLA_POLICY_SOCKET="${VLA_POLICY_SOCKET:-${VLA_RUNTIME_ROOT}/policy/policy.sock}"
export VLA_EPISODE_ROOT="${VLA_EPISODE_ROOT:-${VLA_STATE_ROOT}/datasets/episodes}"
export VLA_DEBUG_ROOT="${VLA_DEBUG_ROOT:-${VLA_RUNTIME_ROOT}/ops/debug}"
export VLA_JOBS_ROOT="${VLA_JOBS_ROOT:-${VLA_RUNTIME_ROOT}/ops/jobs}"

prepare_vla_runtime_paths() {
  mkdir -p \
    "$(dirname "${VLA_POLICY_SOCKET}")" \
    "${VLA_EPISODE_ROOT}" \
    "${VLA_DEBUG_ROOT}" \
    "${VLA_JOBS_ROOT}" \
    "${VLA_LOG_ROOT}/components"
}
