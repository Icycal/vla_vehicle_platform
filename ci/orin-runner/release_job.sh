#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
release_version="${VLA_RELEASE_VERSION:-}"

if [[ $# -gt 0 ]]; then
  release_version="$1"
fi

cd "${PROJECT_ROOT}"

if [[ -n "$(git status --porcelain)" ]]; then
  echo "Runner checkout is dirty." >&2
  exit 1
fi

"${PROJECT_ROOT}/scripts/prepare_orin_runner.sh"

lock_file="${HOME}/.cache/vla-vehicle-release.lock"
mkdir -p "$(dirname "${lock_file}")"
exec 9>"${lock_file}"
if ! flock -n 9; then
  echo "Another VLA release build is already running." >&2
  exit 1
fi

arguments=()
if [[ -n "${release_version}" ]]; then
  arguments+=(--version "${release_version}")
fi

"${PROJECT_ROOT}/scripts/build_release.sh" "${arguments[@]}"

if [[ -n "${release_version}" ]]; then
  artifact_root="${PROJECT_ROOT}/artifacts/releases/${release_version}"
else
  artifact_root="$(find "${PROJECT_ROOT}/artifacts/releases" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' | sort -nr | head -1 | cut -d' ' -f2-)"
fi

printf 'artifact_root=%s\n' "${artifact_root}"
find "${artifact_root}" -maxdepth 1 -type f -printf '%f\n' | sort
printf 'ORIN_RELEASE_JOB_PASS\n'
