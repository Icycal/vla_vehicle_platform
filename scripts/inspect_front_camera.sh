#!/usr/bin/env bash
set -eo pipefail

DEVICE="${1:-/dev/video0}"

test -e "${DEVICE}"
echo "Device: ${DEVICE}"
v4l2-ctl -d "${DEVICE}" --all
v4l2-ctl -d "${DEVICE}" --list-formats-ext
