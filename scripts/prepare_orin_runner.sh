#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "The release runner must be a native aarch64 host." >&2
  exit 1
fi

if [[ ! -f /etc/nv_tegra_release ]]; then
  echo "Jetson L4T release metadata is missing." >&2
  exit 1
fi

for command_name in git docker colcon cmake zstd sha256sum python3; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "Missing runner dependency: ${command_name}" >&2
    exit 1
  fi
done

if [[ ! -f /opt/ros/humble/setup.bash ]]; then
  echo "ROS 2 Humble is not installed." >&2
  exit 1
fi

if ! docker info >/dev/null 2>&1; then
  echo "Docker is unavailable to the runner user." >&2
  exit 1
fi

available_gb="$(df -Pk "${PROJECT_ROOT}" | awk 'NR==2 {print int($4 / 1024 / 1024)}')"
if (( available_gb < 25 )); then
  echo "At least 25 GiB free space is required; found ${available_gb} GiB." >&2
  exit 1
fi

printf 'architecture=%s\n' "$(uname -m)"
printf 'l4t=%s\n' "$(head -1 /etc/nv_tegra_release)"
printf 'ros=humble\n'
printf 'docker=%s\n' "$(docker version --format '{{.Server.Version}}')"
printf 'free_space_gb=%s\n' "${available_gb}"
printf 'ORIN_RUNNER_READY\n'
