#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
INSTALLER="${SCRIPT_DIR}/install_release.sh"
if [[ ! -f "${INSTALLER}" ]]; then
  INSTALLER="${PROJECT_ROOT}/scripts/install_release.sh"
fi
target="${VLA_DEPLOY_TARGET:-}"
archive=""
policy_image=""
non_interactive=false

usage() {
  cat <<'EOF'
Usage: deploy_release.sh --archive FILE [options]

Options:
  --target USER@HOST     Deployment target.
  --policy-image FILE    Exported Policy Runtime OCI archive.
  --non-interactive      Use sudo -n for CI deployment.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --archive) archive="${2:?}"; shift 2 ;;
    --target) target="${2:?}"; shift 2 ;;
    --policy-image) policy_image="${2:?}"; shift 2 ;;
    --non-interactive) non_interactive=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "${target}" ]]; then
  echo "Deployment target is required. Use --target USER@HOST or VLA_DEPLOY_TARGET." >&2
  exit 2
fi

if [[ -z "${archive}" || ! -f "${archive}" ]]; then
  echo "Release archive not found: ${archive}" >&2
  exit 2
fi

archive="$(readlink -f "${archive}")"
remote_dir="/tmp/vla-release-upload-$(date +%s)-$$"

ssh "${target}" "mkdir -p '${remote_dir}'"
cleanup() {
  ssh "${target}" "rm -rf '${remote_dir}'" >/dev/null 2>&1 || true
}
trap cleanup EXIT

files=("${archive}")
if ! $non_interactive; then
  files+=("${INSTALLER}")
fi
[[ -f "${archive}.sha256" ]] && files+=("${archive}.sha256")
if [[ -n "${policy_image}" ]]; then
  policy_image="$(readlink -f "${policy_image}")"
  files+=("${policy_image}")
  [[ -f "${policy_image}.sha256" ]] && files+=("${policy_image}.sha256")
fi

scp "${files[@]}" "${target}:${remote_dir}/"

if $non_interactive; then
  install_command=(
    /usr/local/sbin/vla-install-release
    --archive "${remote_dir}/$(basename "${archive}")"
    --activate
    --enable-services
  )
else
  install_command=(
    bash "${remote_dir}/$(basename "${INSTALLER}")"
    --archive "${remote_dir}/$(basename "${archive}")"
    --activate
    --enable-services
  )
fi
if [[ -n "${policy_image}" ]]; then
  install_command+=(--policy-image "${remote_dir}/$(basename "${policy_image}")")
fi

printf -v remote_command '%q ' "${install_command[@]}"
if $non_interactive; then
  ssh "${target}" "sudo -n ${remote_command}"
else
  ssh -t "${target}" "sudo ${remote_command}"
fi

echo "Deployment completed on ${target}."
