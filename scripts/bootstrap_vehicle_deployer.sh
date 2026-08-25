#!/usr/bin/env bash
set -eo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run this one-time bootstrap with sudo." >&2
  exit 1
fi

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPLOY_USER="${VLA_DEPLOY_USER:-${SUDO_USER:-}}"
if [[ -z "${DEPLOY_USER}" ]] || ! id "${DEPLOY_USER}" >/dev/null 2>&1; then
  echo "Set VLA_DEPLOY_USER to an existing non-root deployment user." >&2
  exit 1
fi
install -o root -g root -m 0755 \
  "${PROJECT_ROOT}/scripts/install_release.sh" \
  /usr/local/sbin/vla-install-release
install -o root -g root -m 0755 \
  "${PROJECT_ROOT}/scripts/rollback_release.sh" \
  /usr/local/sbin/vla-rollback-release
sed "s|@VLA_DEPLOY_USER@|${DEPLOY_USER}|g" \
  "${PROJECT_ROOT}/deploy/sudoers/vla-deploy" > /etc/sudoers.d/vla-deploy
chown root:root /etc/sudoers.d/vla-deploy
chmod 0440 /etc/sudoers.d/vla-deploy
visudo -cf /etc/sudoers.d/vla-deploy

echo "Installed restricted VLA deployment helpers."
