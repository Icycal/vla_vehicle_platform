if [[ -f /etc/bash.bashrc ]]; then
  source /etc/bash.bashrc
fi

export HISTFILE="${HOME}/.bash_history"
export PS1="[ros:${ROS_ENV_PROFILE}|domain:${ROS_DOMAIN_ID}] \u@\h:\w\\$ "

printf 'ROS profile: %s\n' "${ROS_ENV_PROFILE}"
printf 'ROS domain: %s\n' "${ROS_DOMAIN_ID}"
printf 'Workspace head: %s\n' "${COLCON_PREFIX_PATH:-/opt/ros/humble}"
printf 'Exit this shell with: exit\n'
