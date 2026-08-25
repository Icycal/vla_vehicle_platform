#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROVIDER="${1:?provider is required}"
shift
case "${PROVIDER}" in
  smolvla) exec "${PROJECT_ROOT}/scripts/activate_smolvla_model.sh" "$@" ;;
  *) echo "Unsupported model provider: ${PROVIDER}" >&2; exit 2 ;;
esac