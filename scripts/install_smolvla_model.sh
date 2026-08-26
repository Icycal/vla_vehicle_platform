#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:?model version is required}"
MODEL_ID="${2:?model repository id is required}"
REVISION="${3:-main}"
DATASET_ID="${4:-}"
IMAGE="${LEROBOT_IMAGE:-vla-lerobot-compat:0.4.3}"
MODEL_PARENT="${PROJECT_ROOT}/run/models/providers/smolvla"
TARGET="${MODEL_PARENT}/${VERSION}"
PARTIAL="${TARGET}.partial"
DOWNLOAD_ENV="${PROJECT_ROOT}/run/config/model-download.env"

[[ "${VERSION}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$ ]] || { echo "Invalid model version" >&2; exit 2; }
[[ "${MODEL_ID}" =~ ^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$ ]] || { echo "Invalid model repository id" >&2; exit 2; }
[[ "${REVISION}" =~ ^[A-Za-z0-9._/-]{1,120}$ ]] || { echo "Invalid model revision" >&2; exit 2; }
if [[ -n "${DATASET_ID}" && ! "${DATASET_ID}" =~ ^[A-Za-z0-9._/-]{1,160}$ ]]; then echo "Invalid dataset id" >&2; exit 2; fi
if [[ -e "${TARGET}" || -e "${PARTIAL}" ]]; then echo "Model version already exists: ${VERSION}" >&2; exit 2; fi

mkdir -p "${MODEL_PARENT}" "${PROJECT_ROOT}/run/test"
if [[ -f "${DOWNLOAD_ENV}" ]]; then set -a; source "${DOWNLOAD_ENV}"; set +a; fi
mkdir -p "${PARTIAL}"
cleanup() { rm -rf "${PARTIAL}"; }
trap cleanup EXIT

docker run --rm --network host \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp --env HF_HOME=/tmp/huggingface \
  --env HF_ENDPOINT --env HF_TOKEN \
  --env HF_HUB_ETAG_TIMEOUT="${HF_HUB_ETAG_TIMEOUT:-120}" \
  --env HF_HUB_DOWNLOAD_TIMEOUT="${HF_HUB_DOWNLOAD_TIMEOUT:-300}" \
  --volume "${PARTIAL}:/models/target" \
  "${IMAGE}" python3 /opt/vla/download_model.py \
  --repo-id "${MODEL_ID}" --revision "${REVISION}" \
  --output-dir /models/target --max-workers "${SMOLVLA_DOWNLOAD_WORKERS:-2}"

for required in config.json model.safetensors policy_preprocessor.json policy_postprocessor.json; do
  [[ -s "${PARTIAL}/${required}" ]] || { echo "Downloaded model is missing ${required}" >&2; exit 3; }
done

python3 - "${PARTIAL}/vehicle_model_manifest.json" "${VERSION}" "${MODEL_ID}" "${REVISION}" "${DATASET_ID}" <<'PY'
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
path, version, model_id, revision, dataset_id = sys.argv[1:]
manifest_path = Path(path)
manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
manifest.update({"schema_version": "vehicle.policy-model.v1", "version": version, "provider": "smolvla", "model_id": model_id, "revision": revision, "dataset_id": dataset_id, "installed_at": datetime.now(timezone.utc).isoformat(), "state": "installed"})
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
PY

mv "${PARTIAL}" "${TARGET}"
trap - EXIT
echo "Installed SmolVLA model ${VERSION} from ${MODEL_ID}@${REVISION}"