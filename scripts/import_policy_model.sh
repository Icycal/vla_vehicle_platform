#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROVIDER="${1:?provider is required}"
VERSION="${2:?model version is required}"
SOURCE_INPUT="${3:?local model path is required}"
DATASET_ID="${4:-}"

case "${PROVIDER}" in
  smolvla) ;;
  *) echo "Unsupported model provider: ${PROVIDER}" >&2; exit 2 ;;
esac
[[ "${VERSION}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$ ]] || { echo "Invalid model version" >&2; exit 2; }

if [[ "${SOURCE_INPUT}" = /* ]]; then
  SOURCE="${SOURCE_INPUT}"
else
  SOURCE="${PROJECT_ROOT}/${SOURCE_INPUT}"
fi
SOURCE="$(realpath "${SOURCE}")"
[[ -d "${SOURCE}" ]] || { echo "Local model directory not found: ${SOURCE}" >&2; exit 2; }
for required in config.json model.safetensors policy_preprocessor.json policy_postprocessor.json; do
  [[ -s "${SOURCE}/${required}" ]] || { echo "Local model is missing ${required}" >&2; exit 3; }
done

TARGET="${PROJECT_ROOT}/run/models/providers/${PROVIDER}/${VERSION}"
[[ ! -e "${TARGET}" && ! -e "${TARGET}.partial" ]] || {
  echo "Model version already exists: ${VERSION}" >&2; exit 2;
}
mkdir -p "$(dirname "${TARGET}")"
python3 "${PROJECT_ROOT}/tools/model/prepare_model_variant.py" \
  --source "${SOURCE}" \
  --target "${TARGET}" \
  --provider "${PROVIDER}" \
  --weight-precision mixed \
  --dataset-id "${DATASET_ID}"
echo "Imported ${PROVIDER} model ${VERSION} from ${SOURCE}"
