#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$({ cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd; })"
source "${PROJECT_ROOT}/scripts/lib/runtime_paths.sh"
RUNTIME_CONFIG="${VEHICLE_OPS_ENV_FILE:-${PROJECT_ROOT}/run/config/vehicle_ops.env}"

source "${PROJECT_ROOT}/scripts/ensure_vla_environment.sh" 43 "$@"
prepare_vla_runtime_paths

if [[ ! -f "${RUNTIME_CONFIG}" ]]; then
  mkdir -p "$(dirname "${RUNTIME_CONFIG}")"
  if command -v openssl >/dev/null 2>&1; then
    generated_token="$(openssl rand -hex 16)"
  else
    generated_token="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
  fi
  cat > "${RUNTIME_CONFIG}" <<EOF
VEHICLE_OPS_BIND_ADDRESS=0.0.0.0
VEHICLE_OPS_PORT=8088
VEHICLE_OPS_OPERATOR_TOKEN=${generated_token}
VEHICLE_OPS_JOBS_ROOT=${VLA_JOBS_ROOT}
EOF
  chmod 600 "${RUNTIME_CONFIG}"
  echo "Created runtime configuration: ${RUNTIME_CONFIG}"
fi

set -a
source "${RUNTIME_CONFIG}"
set +a

: "${VEHICLE_OPS_BIND_ADDRESS:=0.0.0.0}"
: "${VEHICLE_OPS_PORT:=8088}"
: "${VEHICLE_OPS_OPERATOR_TOKEN:?VEHICLE_OPS_OPERATOR_TOKEN is required}"
: "${VEHICLE_OPS_JOBS_ROOT:=${VLA_JOBS_ROOT}}"

declare -A seen_ips=()
vehicle_ips=()
if [[ -n "${SSH_CONNECTION:-}" ]]; then
  vehicle_ips+=("$(awk '{print $3}' <<<"${SSH_CONNECTION}")")
fi
for candidate in $(hostname -I 2>/dev/null); do
  [[ "${candidate}" == 127.* || "${candidate}" == 172.17.* ]] && continue
  vehicle_ips+=("${candidate}")
done
echo "Vehicle Ops Console URLs:"
for vehicle_ip in "${vehicle_ips[@]}"; do
  [[ -z "${vehicle_ip}" || -n "${seen_ips[${vehicle_ip}]:-}" ]] && continue
  seen_ips["${vehicle_ip}"]=1
  echo "  http://${vehicle_ip}:${VEHICLE_OPS_PORT}"
done
echo "Operator token loaded from protected runtime configuration."
echo "Write operations require the browser session token."

exec ros2 launch vehicle_ops vehicle_ops.launch.xml \
  bind_address:="${VEHICLE_OPS_BIND_ADDRESS}" \
  port:="${VEHICLE_OPS_PORT}" \
  operator_token:="${VEHICLE_OPS_OPERATOR_TOKEN}" \
  project_root:="${PROJECT_ROOT}" \
  state_root:="${VLA_STATE_ROOT}" \
  log_root:="${VLA_LOG_ROOT}" \
  jobs_root:="${VEHICLE_OPS_JOBS_ROOT}" \
  "$@"
