#!/usr/bin/env bash
set -eo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${PROJECT_ROOT}/scripts/lib/runtime_paths.sh"
prepare_vla_runtime_paths
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-43}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
source "${ROS_SETUP_FILE:-/opt/ros/${ROS_DISTRO:-humble}/setup.bash}"
source "${PROJECT_ROOT}/ros_ws/install/local_setup.bash"
MOBILITY_PARAMS_FILE="${VLA_MOBILITY_PARAMS_FILE:-${PROJECT_ROOT}/run/config/mobility.yaml}"
if [[ ! -f "${MOBILITY_PARAMS_FILE}" ]]; then
  MOBILITY_PARAMS_FILE="${PROJECT_ROOT}/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/mobility_twist.yaml"
fi

load_chassis_config() {
  local config_file="${VLA_CHASSIS_ENV_FILE:-${VLA_RUNTIME_ROOT}/config/chassis.env}"
  if [[ -f "${config_file}" ]]; then
    set -a
    source "${config_file}"
    set +a
  fi
  VLA_CHASSIS_PROVIDER="${VLA_CHASSIS_PROVIDER:-wheeltec}"
  VLA_CHASSIS_LAUNCH_PACKAGE="${VLA_CHASSIS_LAUNCH_PACKAGE:-vehicle_bringup}"
  VLA_CHASSIS_LAUNCH_FILE="${VLA_CHASSIS_LAUNCH_FILE:-vehicle_chassis_wheeltec.launch.xml}"
  VLA_CHASSIS_LAUNCH_ARGUMENTS="${VLA_CHASSIS_LAUNCH_ARGUMENTS:-}"
}
case "${1:-}" in
  front_camera)
    exec ros2 launch vehicle_bringup front_camera.launch.xml
    ;;
  vehicle_chassis)
    load_chassis_config
    read -r -a chassis_launch_arguments <<< "${VLA_CHASSIS_LAUNCH_ARGUMENTS}"
    exec ros2 launch "${VLA_CHASSIS_LAUNCH_PACKAGE}" "${VLA_CHASSIS_LAUNCH_FILE}" "${chassis_launch_arguments[@]}"
    ;;
  runtime_core)
    exec ros2 launch vehicle_bringup phase0.launch.xml \
      policy_params_file:="${PROJECT_ROOT}/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/policy_smolvla_shadow.yaml" \
      policy_socket_path:="${VLA_POLICY_SOCKET}" \
      mobility_params_file:="${MOBILITY_PARAMS_FILE}"
    ;;
  observation_pipeline)
    exec ros2 launch vehicle_bringup observation_pipeline.launch.xml
    ;;
  vla_debug_pipeline)
    exec ros2 launch vehicle_bringup vla_debug_pipeline.launch.xml \
      policy_params_file:="${PROJECT_ROOT}/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/policy_smolvla_shadow.yaml" \
      policy_socket_path:="${VLA_POLICY_SOCKET}" \
      debug_artifact_root:="${VLA_DEBUG_ROOT}"
    ;;
  shadow_data)
    exec ros2 launch vehicle_bringup shadow_data.launch.xml \
      episode_storage_root:="${VLA_EPISODE_ROOT}"
    ;;
  *)
    echo "Unsupported managed component: ${1:-}" >&2
    exit 64
    ;;
esac