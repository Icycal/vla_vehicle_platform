#!/usr/bin/env bash
set -eo pipefail

OPT_ROOT="${VLA_OPT_ROOT:-/opt/vla-vehicle}"
ETC_ROOT="${VLA_ETC_ROOT:-/etc/vla-vehicle}"
VAR_ROOT="${VLA_VAR_ROOT:-/var/lib/vla-vehicle}"

if [[ ! -L "${OPT_ROOT}/current" ]]; then
  echo "No active VLA release." >&2
  exit 1
fi

release_root="$(readlink -f "${OPT_ROOT}/current")"
VLA_ENV_FILE="${ETC_ROOT}/vehicle.env" \
VLA_STATE_ROOT="${VAR_ROOT}" \
  "${release_root}/bin/check_release.sh"

python3 - "${release_root}/manifest.json" <<'PY'
import json
import sys
manifest = json.load(open(sys.argv[1], encoding='utf-8'))
print(f"active_release={manifest['release_version']}")
print(f"git_commit={manifest['git_commit']}")
print(f"operation_mode={manifest['operation_mode']}")
print(f"policy_provider={manifest['policy_provider']}")
PY
