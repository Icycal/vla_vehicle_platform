#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

version=""
install_root=""
artifact_root=""
policy_image_tag=""
policy_image_archive=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version="${2:?}"; shift 2 ;;
    --install-root) install_root="${2:?}"; shift 2 ;;
    --artifact-root) artifact_root="${2:?}"; shift 2 ;;
    --policy-image-tag) policy_image_tag="${2:?}"; shift 2 ;;
    --policy-image-archive) policy_image_archive="${2:?}"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

for value in version install_root artifact_root policy_image_tag; do
  if [[ -z "${!value}" ]]; then
    echo "Missing required option: ${value}" >&2
    exit 2
  fi
done

install_root="$(readlink -f "${install_root}")"
artifact_root="$(readlink -f "${artifact_root}")"

if [[ ! -f "${install_root}/local_setup.bash" ]]; then
  echo "Invalid ROS install root: ${install_root}" >&2
  exit 1
fi

bundle_name="vla-vehicle-${version}"
stage_root="${artifact_root}/stage"
bundle_root="${stage_root}/${bundle_name}"
archive_name="${bundle_name}-arm64.tar.zst"
archive_path="${artifact_root}/${archive_name}"

if [[ -e "${stage_root}" ]]; then
  echo "Release staging path already exists: ${stage_root}" >&2
  exit 1
fi

mkdir -p \
  "${bundle_root}/ros" \
  "${bundle_root}/bin" \
  "${bundle_root}/compose" \
  "${bundle_root}/config/defaults" \
  "${bundle_root}/systemd" \
  "${bundle_root}/admin"

cp -a "${install_root}" "${bundle_root}/ros/install"
cp -a "${PROJECT_ROOT}/deploy/bin/." "${bundle_root}/bin/"
cp -a "${PROJECT_ROOT}/deploy/compose/." "${bundle_root}/compose/"
cp -a "${PROJECT_ROOT}/deploy/config/." "${bundle_root}/config/defaults/"
cp -a "${PROJECT_ROOT}/deploy/systemd/." "${bundle_root}/systemd/"
cp -a \
  "${PROJECT_ROOT}/scripts/activate_release.sh" \
  "${PROJECT_ROOT}/scripts/rollback_release.sh" \
  "${PROJECT_ROOT}/scripts/check_installed_release.sh" \
  "${bundle_root}/admin/"

chmod +x "${bundle_root}/bin/"*.sh "${bundle_root}/admin/"*.sh

git_commit="$(git -C "${PROJECT_ROOT}" rev-parse HEAD)"
l4t_release="$(head -1 /etc/nv_tegra_release 2>/dev/null || echo unknown)"
created_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

VERSION="${version}" \
GIT_COMMIT="${git_commit}" \
L4T_RELEASE="${l4t_release}" \
CREATED_AT="${created_at}" \
POLICY_IMAGE_TAG="${policy_image_tag}" \
POLICY_IMAGE_ARCHIVE="${policy_image_archive}" \
python3 - "${bundle_root}/manifest.json" <<'PY'
import json
import os
import platform
import sys
from pathlib import Path

manifest = {
    'schema_version': 'vla.release.v1',
    'release_version': os.environ['VERSION'],
    'git_commit': os.environ['GIT_COMMIT'],
    'created_at': os.environ['CREATED_AT'],
    'architecture': platform.machine(),
    'ubuntu': '22.04',
    'ros_distribution': 'humble',
    'l4t_release': os.environ['L4T_RELEASE'],
    'operation_mode': 'shadow',
    'ros_bundle_layout': 'merge-install',
    'policy_provider': 'mock',
    'policy_image': os.environ['POLICY_IMAGE_TAG'],
    'policy_image_archive': os.environ['POLICY_IMAGE_ARCHIVE'] or None,
    'model_artifacts_included': False,
    'source_tree_required': False,
}
Path(sys.argv[1]).write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
PY

(
  cd "${bundle_root}"
  find . -type f ! -name checksums.sha256 -print0 |
    sort -z |
    xargs -0 sha256sum > checksums.sha256
)

tar --zstd -cf "${archive_path}" -C "${stage_root}" "${bundle_name}"
(
  cd "${artifact_root}"
  sha256sum "${archive_name}" > "${archive_name}.sha256"
)

if [[ -n "${policy_image_archive}" ]]; then
  (
    cd "${artifact_root}"
    sha256sum "${policy_image_archive}" > "${policy_image_archive}.sha256"
  )
fi

cp -a \
  "${PROJECT_ROOT}/scripts/install_release.sh" \
  "${PROJECT_ROOT}/scripts/deploy_release.sh" \
  "${PROJECT_ROOT}/scripts/rollback_release.sh" \
  "${PROJECT_ROOT}/scripts/rollback_remote_release.sh" \
  "${artifact_root}/"
chmod +x "${artifact_root}/"*.sh

rm -rf "${stage_root}"

echo "Created ${archive_path}"
