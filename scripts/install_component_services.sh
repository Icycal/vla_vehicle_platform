#!/usr/bin/env bash
set -eo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${PROJECT_ROOT}/scripts/lib/runtime_paths.sh"
UNIT_ROOT="${HOME}/.config/systemd/user"
prepare_vla_runtime_paths
mkdir -p "${UNIT_ROOT}"

escape_sed_replacement() {
  printf '%s' "$1" | sed 's/[\\&|]/\\&/g'
}

project_root_escaped="$(escape_sed_replacement "${PROJECT_ROOT}")"
state_root_escaped="$(escape_sed_replacement "${VLA_STATE_ROOT}")"
runtime_root_escaped="$(escape_sed_replacement "${VLA_RUNTIME_ROOT}")"
log_root_escaped="$(escape_sed_replacement "${VLA_LOG_ROOT}")"

for template in "${PROJECT_ROOT}"/deploy/systemd/user/*.service; do
  unit_path="${UNIT_ROOT}/$(basename "${template}")"
  sed \
    -e "s|@VLA_PROJECT_ROOT@|${project_root_escaped}|g" \
    -e "s|@VLA_STATE_ROOT@|${state_root_escaped}|g" \
    -e "s|@VLA_RUNTIME_ROOT@|${runtime_root_escaped}|g" \
    -e "s|@VLA_LOG_ROOT@|${log_root_escaped}|g" \
    "${template}" > "${unit_path}"
  chmod 0644 "${unit_path}"
done
chmod +x "${PROJECT_ROOT}/scripts/run_managed_component.sh"
systemctl --user daemon-reload
systemctl --user enable vla-ops-console.service
printf 'Installed component services into %s\n' "${UNIT_ROOT}"
printf 'Management plane enabled; managed vehicle components remain disabled at boot.\n'
