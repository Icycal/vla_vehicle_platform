#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_ROOT="${PROJECT_ROOT}/ros_ws/src"
VENDOR_ROOT="${SOURCE_ROOT}/vendor"
WHEELTEC_ROOT="${PROJECT_ROOT}/third_party/wheeltec_ros2"

require_directory() {
  if [[ ! -d "$1" ]]; then
    echo "Missing vendored source directory: $1" >&2
    exit 1
  fi
}

require_directory "${WHEELTEC_ROOT}/navigation2-humble"
require_directory "${WHEELTEC_ROOT}/depend/serial_ros2"
require_directory "${WHEELTEC_ROOT}/usb_cam-ros2"
require_directory "${WHEELTEC_ROOT}/turn_on_wheeltec_robot"
require_directory "${WHEELTEC_ROOT}/wheeltec_robot_msg"

mkdir -p "${VENDOR_ROOT}/nav2" "${VENDOR_ROOT}/wheeltec"
find "${VENDOR_ROOT}" -type l -delete

while IFS= read -r -d '' manifest; do
  package_directory="$(dirname "${manifest}")"
  package_name="$(basename "${package_directory}")"
  ln -s "${package_directory}" "${VENDOR_ROOT}/nav2/${package_name}"
done < <(find "${WHEELTEC_ROOT}/navigation2-humble" -mindepth 2 -name package.xml -print0)

ln -s "${WHEELTEC_ROOT}/depend/serial_ros2" "${VENDOR_ROOT}/wheeltec/serial_ros2"
ln -s "${WHEELTEC_ROOT}/usb_cam-ros2" "${VENDOR_ROOT}/wheeltec/usb_cam-ros2"
ln -s "${WHEELTEC_ROOT}/wheeltec_robot_msg" "${VENDOR_ROOT}/wheeltec/wheeltec_robot_msg"
ln -s "${WHEELTEC_ROOT}/turn_on_wheeltec_robot" "${VENDOR_ROOT}/wheeltec/turn_on_wheeltec_robot"

echo "Vendored ROS 2 sources linked into ${VENDOR_ROOT}"
echo "No legacy workspace install was sourced."
