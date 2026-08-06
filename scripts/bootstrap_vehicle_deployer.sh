#!/usr/bin/env bash
set -eo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run this one-time bootstrap with sudo." >&2
  exit 1
fi

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
install -o root -g root -m 0755 \
  "${PROJECT_ROOT}/scripts/install_release.sh" \
  /usr/local/sbin/vla-install-release
install -o root -g root -m 0755 \
  "${PROJECT_ROOT}/scripts/rollback_release.sh" \
  /usr/local/sbin/vla-rollback-release
install -o root -g root -m 0440 \
  "${PROJECT_ROOT}/deploy/sudoers/vla-deploy" \
  /etc/sudoers.d/vla-deploy
visudo -cf /etc/sudoers.d/vla-deploy

echo "Installed restricted VLA deployment helpers."
