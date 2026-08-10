#!/usr/bin/env bash
set -eo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_ROOT="${HOME}/.config/systemd/user"
mkdir -p "${UNIT_ROOT}" "${PROJECT_ROOT}/run/log/components"
install -m 0644 "${PROJECT_ROOT}"/deploy/systemd/user/*.service "${UNIT_ROOT}/"
chmod +x "${PROJECT_ROOT}/scripts/run_managed_component.sh"
systemctl --user daemon-reload
systemctl --user enable vla-ops-console.service
printf 'Installed component services into %s\n' "${UNIT_ROOT}"
printf 'Management plane enabled; managed vehicle components remain disabled at boot.\n'