#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROVIDER="${1:?provider is required}"
SOURCE_VERSION="${2:?source version is required}"
TARGET_VERSION="${3:?target version is required}"
PRECISION="${4:-int8}"
SOURCE="${PROJECT_ROOT}/run/models/providers/${PROVIDER}/${SOURCE_VERSION}"
TARGET="${PROJECT_ROOT}/run/models/providers/${PROVIDER}/${TARGET_VERSION}"

case "${PROVIDER}" in
  smolvla) ;;
  *) echo "Unsupported model provider: ${PROVIDER}" >&2; exit 2 ;;
esac

exec python3 "${PROJECT_ROOT}/tools/model/prepare_model_variant.py" \
  --source "${SOURCE}" \
  --target "${TARGET}" \
  --provider "${PROVIDER}" \
  --weight-precision "${PRECISION}" \
  --target-platform linux
