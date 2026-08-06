#!/usr/bin/env bash
set -eo pipefail

RELEASE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${VLA_ENV_FILE:-/etc/vla-vehicle/vehicle.env}"

if [[ -f "${ENV_FILE}" ]]; then
  set -a
  source "${ENV_FILE}"
  set +a
fi

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
export VLA_CONFIG_ROOT="${VLA_CONFIG_ROOT:-/etc/vla-vehicle}"
export VLA_STATE_ROOT="${VLA_STATE_ROOT:-/var/lib/vla-vehicle}"

source /opt/ros/humble/setup.bash
source "${RELEASE_ROOT}/ros/install/local_setup.bash"

exec ros2 launch vehicle_bringup phase1_shadow.launch.xml \
  camera_params:="${VLA_CONFIG_ROOT}/front_camera.yaml" \
  shadow_params:="${VLA_CONFIG_ROOT}/phase1_shadow.yaml" \
  policy_params_file:="${VLA_CONFIG_ROOT}/policy_socket.yaml"
