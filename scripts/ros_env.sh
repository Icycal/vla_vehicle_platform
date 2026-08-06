#!/usr/bin/env bash
set -eo pipefail

SCRIPT_PATH="$(readlink -f "${BASH_SOURCE[0]}")"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHELL_RC="${PROJECT_ROOT}/config/ros_env_shell.bashrc"

usage() {
  cat <<'EOF'
Usage:
  ros_env.sh --list
  ros_env.sh PROFILE [--domain DOMAIN_ID]
  ros_env.sh PROFILE [--domain DOMAIN_ID] -- COMMAND [ARG...]

Profiles:
  base       ROS 2 Humble only, default domain 0
  wheeltec   Wheeltec workspace, default domain 0
  autoware   Wheeltec + Autoware, default domain 0
  rhzd       Wheeltec + Autoware + RHZD, default domain 0
  vla        VLA vehicle platform only, default domain 43

Examples:
  ./scripts/ros_env.sh vla
  ./scripts/ros_env.sh wheeltec
  ./scripts/ros_env.sh rhzd
  ./scripts/ros_env.sh vla -- ros2 pkg prefix usb_cam
  ./scripts/ros_env.sh vla --domain 42 -- ros2 node list
EOF
}

profile_default_domain() {
  case "$1" in
    base|wheeltec|autoware|rhzd)
      echo 0
      ;;
    vla)
      echo 43
      ;;
    *)
      return 1
      ;;
  esac
}

require_setup() {
  if [[ ! -f "$1" ]]; then
    echo "ROS environment setup not found: $1" >&2
    exit 1
  fi
  source "$1"
}

load_profile() {
  local profile="$1"

  require_setup /opt/ros/humble/setup.bash

  case "${profile}" in
    base)
      ;;
    wheeltec)
      require_setup /home/wheeltec/wheeltec_ros2/install/local_setup.bash
      ;;
    autoware)
      require_setup /home/wheeltec/wheeltec_ros2/install/local_setup.bash
      require_setup /home/wheeltec/autoware/install/local_setup.bash
      ;;
    rhzd)
      require_setup /home/wheeltec/wheeltec_ros2/install/local_setup.bash
      require_setup /home/wheeltec/autoware/install/local_setup.bash
      require_setup /home/wheeltec/workspace_rhzd/install/local_setup.bash
      ;;
    vla)
      require_setup "${PROJECT_ROOT}/ros_ws/install/local_setup.bash"
      ;;
  esac
}

append_if_set() {
  local variable_name="$1"
  if [[ -v "${variable_name}" ]]; then
    CLEAN_ENV+=("${variable_name}=${!variable_name}")
  fi
}

if [[ "${1:-}" == "--internal" ]]; then
  profile="$2"
  domain_id="$3"
  start_directory="$4"
  shift 4

  load_profile "${profile}"

  export ROS_ENV_ISOLATED=1
  export ROS_ENV_PROFILE="${profile}"
  export ROS_ENV_START_DIRECTORY="${start_directory}"
  export ROS_DOMAIN_ID="${domain_id}"
  export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
  export ROS_LOCALHOST_ONLY=0

  cd "${start_directory}"

  if [[ "${1:-}" == "--" ]]; then
    shift
    if [[ $# -eq 0 ]]; then
      echo "No command supplied after --" >&2
      exit 2
    fi
    exec "$@"
  fi

  exec /bin/bash --noprofile --rcfile "${SHELL_RC}" -i
fi

if [[ $# -eq 0 || "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ "${1}" == "--list" ]]; then
  printf '%s\n' base wheeltec autoware rhzd vla
  exit 0
fi

profile="$1"
shift

if ! default_domain="$(profile_default_domain "${profile}")"; then
  echo "Unknown ROS environment profile: ${profile}" >&2
  usage >&2
  exit 2
fi

domain_id="${default_domain}"
if [[ "${1:-}" == "--domain" ]]; then
  if [[ -z "${2:-}" || ! "${2}" =~ ^[0-9]+$ ]]; then
    echo "--domain requires a non-negative integer" >&2
    exit 2
  fi
  domain_id="$2"
  shift 2
fi

if [[ $# -gt 0 && "${1}" != "--" ]]; then
  echo "Expected -- before command" >&2
  usage >&2
  exit 2
fi

CLEAN_ENV=(
  "HOME=${HOME}"
  "USER=${USER:-wheeltec}"
  "LOGNAME=${LOGNAME:-${USER:-wheeltec}}"
  "SHELL=/bin/bash"
  "PATH=/usr/local/cuda/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
  "LANG=${LANG:-C.UTF-8}"
  "ROS_ENV_PROJECT_ROOT=${PROJECT_ROOT}"
)

for variable_name in \
  TERM DISPLAY WAYLAND_DISPLAY XAUTHORITY XDG_RUNTIME_DIR \
  DBUS_SESSION_BUS_ADDRESS SSH_AUTH_SOCK; do
  append_if_set "${variable_name}"
done

exec env -i "${CLEAN_ENV[@]}" \
  /bin/bash --noprofile --norc "${SCRIPT_PATH}" \
  --internal "${profile}" "${domain_id}" "${PWD}" "$@"
