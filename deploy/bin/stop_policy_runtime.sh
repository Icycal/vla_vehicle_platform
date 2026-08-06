#!/usr/bin/env bash
set -eo pipefail

RELEASE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${VLA_ENV_FILE:-/etc/vla-vehicle/vehicle.env}"

if [[ -f "${ENV_FILE}" ]]; then
  set -a
  source "${ENV_FILE}"
  set +a
fi

export VLA_STATE_ROOT="${VLA_STATE_ROOT:-/var/lib/vla-vehicle}"
export POLICY_RUNTIME_UID="${POLICY_RUNTIME_UID:-$(id -u)}"
export POLICY_RUNTIME_GID="${POLICY_RUNTIME_GID:-$(id -g)}"
export VLA_POLICY_IMAGE="${VLA_POLICY_IMAGE:-$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["policy_image"])' "${RELEASE_ROOT}/manifest.json")}"

docker compose \
  -p vla-policy-runtime \
  --env-file "${ENV_FILE}" \
  -f "${RELEASE_ROOT}/compose/policy-runtime.yaml" \
  down
