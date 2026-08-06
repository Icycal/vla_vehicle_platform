#!/usr/bin/env bash
set -eo pipefail

OPT_ROOT="${VLA_OPT_ROOT:-/opt/vla-vehicle}"
SKIP_SYSTEMD="${VLA_SKIP_SYSTEMD:-0}"

if [[ "$(readlink -f "$0")" == "/usr/local/sbin/vla-rollback-release" ]]; then
  OPT_ROOT=/opt/vla-vehicle
  SKIP_SYSTEMD=0
fi

if [[ ! -L "${OPT_ROOT}/previous" ]]; then
  echo "No previous release is available." >&2
  exit 1
fi

previous_link="$(readlink "${OPT_ROOT}/previous")"
if [[ ! -d "${OPT_ROOT}/${previous_link}" ]]; then
  echo "Previous release target is missing: ${previous_link}" >&2
  exit 1
fi

current_link=""
if [[ -L "${OPT_ROOT}/current" ]]; then
  current_link="$(readlink "${OPT_ROOT}/current")"
fi

current_tmp="${OPT_ROOT}/.current.$$.tmp"
ln -s "${previous_link}" "${current_tmp}"
mv -Tf "${current_tmp}" "${OPT_ROOT}/current"

if [[ -n "${current_link}" ]]; then
  previous_tmp="${OPT_ROOT}/.previous.$$.tmp"
  ln -s "${current_link}" "${previous_tmp}"
  mv -Tf "${previous_tmp}" "${OPT_ROOT}/previous"
fi

if [[ "${SKIP_SYSTEMD}" != "1" ]]; then
  systemctl restart vla-policy-runtime.service vla-ros-shadow.service
fi

echo "Rolled back to ${previous_link}"
