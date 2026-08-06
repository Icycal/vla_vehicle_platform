#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ORIGINAL_ARGUMENTS=("$@")

usage() {
  cat <<'EOF'
Usage: build_release.sh [options]

Options:
  --version VERSION       Release version; defaults to git describe.
  --skip-policy-image     Do not build/export the Mock Policy Runtime image.
  --allow-dirty           Allow a release from a dirty Git working tree.
  --keep-work             Preserve release build directories.
  -h, --help              Show this help.
EOF
}

version=""
build_policy_image=true
allow_dirty=false
keep_work=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      version="${2:?--version requires a value}"
      shift 2
      ;;
    --skip-policy-image)
      build_policy_image=false
      shift
      ;;
    --allow-dirty)
      allow_dirty=true
      shift
      ;;
    --keep-work)
      keep_work=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "${ROS_ENV_ISOLATED:-0}" != "1" || "${ROS_ENV_PROFILE:-}" != "base" ]]; then
  exec "${PROJECT_ROOT}/scripts/ros_env.sh" base -- \
    "$0" "${ORIGINAL_ARGUMENTS[@]}"
fi

cd "${PROJECT_ROOT}"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "Release artifacts must be built on a native aarch64 host." >&2
  exit 1
fi

if ! $allow_dirty && [[ -n "$(git status --porcelain)" ]]; then
  echo "Refusing to build a release from a dirty Git working tree." >&2
  exit 1
fi

if [[ -z "${version}" ]]; then
  version="$(git describe --tags --always --dirty)"
fi
version="${version//\//-}"

if [[ ! "${version}" =~ ^[A-Za-z0-9._+-]+$ ]]; then
  echo "Invalid release version: ${version}" >&2
  exit 2
fi

work_root="${PROJECT_ROOT}/artifacts/release-work/${version}"
artifact_root="${PROJECT_ROOT}/artifacts/releases/${version}"

case "${work_root}" in
  "${PROJECT_ROOT}/artifacts/release-work/"*) ;;
  *) echo "Unsafe release work path: ${work_root}" >&2; exit 1 ;;
esac

if [[ -e "${work_root}" || -e "${artifact_root}" ]]; then
  echo "Release output already exists for ${version}." >&2
  exit 1
fi

mkdir -p "${work_root}" "${artifact_root}"
trap 'if ! $keep_work; then rm -rf "${work_root}"; fi' EXIT

"${PROJECT_ROOT}/scripts/prepare_workspace.sh"

colcon --log-base "${work_root}/log" build \
  --merge-install \
  --base-paths "${PROJECT_ROOT}/ros_ws/src" \
  --build-base "${work_root}/build" \
  --install-base "${work_root}/install" \
  --packages-up-to vehicle_bringup turn_on_wheeltec_robot \
  --allow-overriding nav2_common nav2_msgs \
  --cmake-args -DCMAKE_BUILD_TYPE=Release

external_links="$(
  find "${work_root}/install" -type l -printf '%p -> %l\n' |
    grep -E "${PROJECT_ROOT}/(ros_ws/src|ros_ws/build)|${work_root}/build" || true
)"
if [[ -n "${external_links}" ]]; then
  echo "Release install contains development-tree symlinks:" >&2
  printf '%s\n' "${external_links}" >&2
  exit 1
fi

policy_image_tag="vla-policy-runtime:${version}-arm64"
policy_image_archive=""

if $build_policy_image; then
  docker build \
    --pull=false \
    --network=host \
    --file policy-runtime/Dockerfile.mock \
    --tag "${policy_image_tag}" \
    .

  policy_image_archive="vla-policy-runtime-${version}-arm64.oci.tar.zst"
  docker save "${policy_image_tag}" |
    zstd -T0 -19 -o "${artifact_root}/${policy_image_archive}"
fi

package_arguments=(
  --version "${version}"
  --install-root "${work_root}/install"
  --artifact-root "${artifact_root}"
  --policy-image-tag "${policy_image_tag}"
)
if [[ -n "${policy_image_archive}" ]]; then
  package_arguments+=(--policy-image-archive "${policy_image_archive}")
fi

"${PROJECT_ROOT}/scripts/package_release.sh" "${package_arguments[@]}"

echo "Release artifacts: ${artifact_root}"
