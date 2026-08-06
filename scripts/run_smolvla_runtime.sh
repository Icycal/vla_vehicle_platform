#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${SMOLVLA_RUNTIME_ENV_FILE:-${PROJECT_ROOT}/run/config/smolvla-runtime.env}"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "Missing runtime configuration: ${ENV_FILE}" >&2
  echo "Copy config/smolvla-runtime.env.example to run/config/smolvla-runtime.env." >&2
  exit 1
fi

mkdir -p "${PROJECT_ROOT}/run/policy"
rm -f "${PROJECT_ROOT}/run/policy/policy.sock"
export POLICY_RUNTIME_UID="${POLICY_RUNTIME_UID:-$(id -u)}"
export POLICY_RUNTIME_GID="${POLICY_RUNTIME_GID:-$(id -g)}"

exec docker compose   -p vla-smolvla-runtime   --env-file "${ENV_FILE}"   -f "${PROJECT_ROOT}/policy-runtime/compose.smolvla-runtime.yaml"   up -d --remove-orphans
