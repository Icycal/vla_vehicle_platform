#!/usr/bin/env bash
set -eo pipefail

OPT_ROOT="${VLA_OPT_ROOT:-/opt/vla-vehicle}"
ETC_ROOT="${VLA_ETC_ROOT:-/etc/vla-vehicle}"
VAR_ROOT="${VLA_VAR_ROOT:-/var/lib/vla-vehicle}"
SKIP_SYSTEMD="${VLA_SKIP_SYSTEMD:-0}"
version="${1:?release version is required}"

release_dir="${OPT_ROOT}/releases/${version}"
if [[ ! -d "${release_dir}" ]]; then
  echo "Release not installed: ${release_dir}" >&2
  exit 1
fi

VLA_ENV_FILE="${ETC_ROOT}/vehicle.env" \
VLA_STATE_ROOT="${VAR_ROOT}" \
  "${release_dir}/bin/check_release.sh"

old_link=""
if [[ -L "${OPT_ROOT}/current" ]]; then
  old_link="$(readlink "${OPT_ROOT}/current")"
fi

new_link="releases/${version}"
current_tmp="${OPT_ROOT}/.current.$$.tmp"
ln -s "${new_link}" "${current_tmp}"
mv -Tf "${current_tmp}" "${OPT_ROOT}/current"

if [[ -n "${old_link}" && "${old_link}" != "${new_link}" ]]; then
  previous_tmp="${OPT_ROOT}/.previous.$$.tmp"
  ln -s "${old_link}" "${previous_tmp}"
  mv -Tf "${previous_tmp}" "${OPT_ROOT}/previous"
fi

if [[ "${SKIP_SYSTEMD}" != "1" ]]; then
  if ! systemctl restart vla-policy-runtime.service vla-ros-shadow.service; then
    if [[ -n "${old_link}" ]]; then
      rollback_tmp="${OPT_ROOT}/.rollback.$$.tmp"
      ln -s "${old_link}" "${rollback_tmp}"
      mv -Tf "${rollback_tmp}" "${OPT_ROOT}/current"
      systemctl restart vla-policy-runtime.service vla-ros-shadow.service || true
    else
      rm -f "${OPT_ROOT}/current"
    fi
    echo "Release activation failed; previous link restored." >&2
    exit 1
  fi
fi

echo "Activated VLA release ${version}"
