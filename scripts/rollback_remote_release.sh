#!/usr/bin/env bash
set -eo pipefail

target="${1:-${VLA_DEPLOY_TARGET:-}}"
non_interactive="${VLA_DEPLOY_NON_INTERACTIVE:-0}"

if [[ -z "${target}" ]]; then
  echo "Rollback target is required. Pass USER@HOST or set VLA_DEPLOY_TARGET." >&2
  exit 2
fi

if [[ "${non_interactive}" == "1" ]]; then
  exec ssh "${target}" "sudo -n /usr/local/sbin/vla-rollback-release"
fi

remote_script="/tmp/vla-rollback-release-$$.sh"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
local_script="${script_dir}/rollback_release.sh"
if [[ ! -f "${local_script}" ]]; then
  local_script="$(cd "${script_dir}/.." && pwd)/scripts/rollback_release.sh"
fi

scp "${local_script}" "${target}:${remote_script}"
cleanup() {
  ssh "${target}" "rm -f '${remote_script}'" >/dev/null 2>&1 || true
}
trap cleanup EXIT
ssh -t "${target}" "sudo bash '${remote_script}'"
