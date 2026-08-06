#!/usr/bin/env bash
set -eo pipefail

RELEASE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

(
  cd "${RELEASE_ROOT}"
  sha256sum --check --quiet checksums.sha256
)

source /opt/ros/humble/setup.bash
source "${RELEASE_ROOT}/ros/install/local_setup.bash"

ros2 pkg prefix vehicle_bringup
ros2 pkg prefix vehicle_runtime
ros2 pkg prefix usb_cam
ros2 pkg prefix turn_on_wheeltec_robot

VLA_STATE_ROOT="${VLA_STATE_ROOT:-/var/lib/vla-vehicle}" \
VLA_POLICY_IMAGE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["policy_image"])' "${RELEASE_ROOT}/manifest.json")" \
POLICY_RUNTIME_UID="$(id -u)" \
POLICY_RUNTIME_GID="$(id -g)" \
docker compose \
  -f "${RELEASE_ROOT}/compose/policy-runtime.yaml" \
  config --quiet

echo VLA_INSTALLED_RELEASE_CHECK_PASS
