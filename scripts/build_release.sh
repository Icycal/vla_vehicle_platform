#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${PROJECT_ROOT}/scripts/lib/platform.sh"
ORIGINAL_ARGUMENTS=("$@")

usage() {
  cat <<'EOF'
Usage: build_release.sh [options]

Options:
  --version VERSION       Release version; defaults to git describe.
  --skip-policy-image     Do not build/export the Mock Policy Runtime image.
  --chassis-provider ID   Chassis package set: wheeltec or none (default: wheeltec).
  --allow-dirty           Allow a release from a dirty Git working tree.
  --keep-work             Preserve release build directories.
  -h, --help              Show this help.
EOF
}

version=""
build_policy_image=true
allow_dirty=false
keep_work=false
chassis_provider="wheeltec"

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
    --chassis-provider)
      chassis_provider="${2:?--chassis-provider requires a value}"
      shift 2
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

machine_arch="$(vla_machine_arch)"
artifact_arch="$(vla_artifact_arch "${machine_arch}")"

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
if [[ "${chassis_provider}" != "wheeltec" && "${chassis_provider}" != "none" ]]; then
  echo "Unsupported chassis provider: ${chassis_provider}" >&2
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

release_packages=(vehicle_bringup)
if [[ "${chassis_provider}" == "wheeltec" ]]; then
  release_packages+=(turn_on_wheeltec_robot)
fi

colcon --log-base "${work_root}/log" build \
  --merge-install \
  --base-paths "${PROJECT_ROOT}/ros_ws/src" \
  --build-base "${work_root}/build" \
  --install-base "${work_root}/install" \
  --packages-up-to "${release_packages[@]}" \
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

policy_image_tag="vla-policy-runtime:${version}-${artifact_arch}"
policy_image_archive=""

if $build_policy_image; then
  docker build \
    --pull=false \
    --network=host \
    --file policy-runtime/Dockerfile.mock \
    --tag "${policy_image_tag}" \
    .

  policy_image_archive="vla-policy-runtime-${version}-${artifact_arch}.oci.tar.zst"
  docker save "${policy_image_tag}" |
    zstd -T0 -19 -o "${artifact_root}/${policy_image_archive}"
fi

package_arguments=(
  --version "${version}"
  --install-root "${work_root}/install"
  --artifact-root "${artifact_root}"
  --policy-image-tag "${policy_image_tag}"
  --artifact-arch "${artifact_arch}"
  --chassis-provider "${chassis_provider}"
)
if [[ -n "${policy_image_archive}" ]]; then
  package_arguments+=(--policy-image-archive "${policy_image_archive}")
fi

"${PROJECT_ROOT}/scripts/package_release.sh" "${package_arguments[@]}"

echo "Release artifacts: ${artifact_root}"
