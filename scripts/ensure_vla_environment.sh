#!/usr/bin/env bash
required_domain_id="${1:?required ROS domain ID is missing}"
shift

if [[ "${ROS_ENV_ISOLATED:-0}" == "1" &&
      "${ROS_ENV_PROFILE:-}" == "vla" &&
      "${ROS_DOMAIN_ID:-}" == "${required_domain_id}" ]]; then
  return 0
fi

caller_path="$(readlink -f "${BASH_SOURCE[1]}")"
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec "${project_root}/scripts/ros_env.sh" \
  vla \
  --domain "${required_domain_id}" \
  -- "${caller_path}" "$@"
