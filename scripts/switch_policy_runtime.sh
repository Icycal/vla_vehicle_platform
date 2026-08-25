#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-}"
MOCK_COMPOSE="${PROJECT_ROOT}/policy-runtime/compose.mock.yaml"
SMOLVLA_COMPOSE="${PROJECT_ROOT}/policy-runtime/compose.smolvla-runtime.yaml"
SMOLVLA_ENV="${SMOLVLA_RUNTIME_ENV_FILE:-${PROJECT_ROOT}/run/config/smolvla-runtime.env}"
LOCK_FILE="${PROJECT_ROOT}/run/policy/runtime-switch.lock"
if [[ "${TARGET}" != "mock" && "${TARGET}" != "smolvla" ]]; then echo "Target provider must be mock or smolvla" >&2; exit 2; fi
if [[ ! -f "${SMOLVLA_ENV}" ]]; then echo "Missing SmolVLA runtime configuration: ${SMOLVLA_ENV}" >&2; exit 2; fi
mkdir -p "${PROJECT_ROOT}/run/policy"
exec 9>"${LOCK_FILE}"
if ! flock -n 9; then echo "Another Policy Runtime switch is already running" >&2; exit 3; fi
export POLICY_RUNTIME_UID="${POLICY_RUNTIME_UID:-$(id -u)}"
export POLICY_RUNTIME_GID="${POLICY_RUNTIME_GID:-$(id -g)}"
mock_compose() { docker compose -p policy-runtime -f "${MOCK_COMPOSE}" "$@"; }
smolvla_compose() { docker compose -p vla-smolvla-runtime --env-file "${SMOLVLA_ENV}" -f "${SMOLVLA_COMPOSE}" "$@"; }
wait_for_health() {
  local container="$1" timeout_seconds="$2" elapsed=0 state
  while (( elapsed < timeout_seconds )); do
    state="$(docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "${container}" 2>/dev/null || true)"
    echo "Waiting for ${container}: ${state:-not-created} (${elapsed}s/${timeout_seconds}s)"
    [[ "${state}" == "healthy" || "${state}" == "running" ]] && return 0
    [[ "${state}" == "unhealthy" || "${state}" == "exited" || "${state}" == "dead" ]] && return 1
    sleep 2; elapsed=$((elapsed + 2))
  done
  return 1
}
stop_all() { smolvla_compose down --remove-orphans || true; mock_compose down --remove-orphans || true; rm -f "${PROJECT_ROOT}/run/policy/policy.sock"; }
start_mock() { echo "Starting Mock Policy Runtime"; mock_compose up -d --remove-orphans; wait_for_health policy-runtime-policy-runtime-1 30; }
start_smolvla() { echo "Starting SmolVLA Policy Runtime"; smolvla_compose up -d --remove-orphans; wait_for_health vla-smolvla-runtime 180; }
rollback_mock() { echo "SmolVLA startup failed; rolling back to Mock" >&2; docker logs --tail 120 vla-smolvla-runtime 2>&1 || true; smolvla_compose down --remove-orphans || true; rm -f "${PROJECT_ROOT}/run/policy/policy.sock"; start_mock; }
stop_all
if [[ "${TARGET}" == "mock" ]]; then start_mock; echo "Policy Runtime switch completed: mock"; exit 0; fi
if start_smolvla; then echo "Policy Runtime switch completed: smolvla"; exit 0; fi
rollback_mock
exit 1
