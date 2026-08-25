#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:?model version is required}"
MODEL_ROOT="${PROJECT_ROOT}/run/models/providers/smolvla/${VERSION}"
ENV_FILE="${SMOLVLA_RUNTIME_ENV_FILE:-${PROJECT_ROOT}/run/config/smolvla-runtime.env}"
BACKUP="${ENV_FILE}.model-activate-backup"

[[ "${VERSION}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$ ]] || { echo "Invalid model version" >&2; exit 2; }
[[ -d "${MODEL_ROOT}" ]] || { echo "Model version not found: ${VERSION}" >&2; exit 2; }
for required in config.json model.safetensors policy_preprocessor.json policy_postprocessor.json; do
  [[ -s "${MODEL_ROOT}/${required}" ]] || { echo "Model is missing ${required}" >&2; exit 3; }
done
[[ -f "${ENV_FILE}" ]] || { echo "Runtime environment file not found: ${ENV_FILE}" >&2; exit 2; }

MODEL_ID="${VERSION}"
if [[ -f "${MODEL_ROOT}/vehicle_model_manifest.json" ]]; then
  MODEL_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8")).get("model_id") or sys.argv[2])' "${MODEL_ROOT}/vehicle_model_manifest.json" "${VERSION}")"
fi
cp -f "${ENV_FILE}" "${BACKUP}"
restore_previous() { cp -f "${BACKUP}" "${ENV_FILE}"; "${PROJECT_ROOT}/scripts/switch_policy_runtime.sh" smolvla || true; rm -f "${BACKUP}"; }
trap restore_previous ERR

python3 - "${ENV_FILE}" "${MODEL_ROOT}" "${MODEL_ID}" <<'PY'
import sys
from pathlib import Path
path = Path(sys.argv[1])
updates = {"SMOLVLA_MODEL_ROOT": sys.argv[2], "SMOLVLA_MODEL_ID": sys.argv[3]}
lines = path.read_text(encoding="utf-8").splitlines()
seen, output = set(), []
for line in lines:
    key = line.split("=", 1)[0] if "=" in line and not line.lstrip().startswith("#") else ""
    if key in updates:
        output.append(f"{key}={updates[key]}"); seen.add(key)
    else:
        output.append(line)
for key, value in updates.items():
    if key not in seen: output.append(f"{key}={value}")
path.write_text("\n".join(output) + "\n", encoding="utf-8")
PY

"${PROJECT_ROOT}/scripts/switch_policy_runtime.sh" smolvla
rm -f "${BACKUP}"
trap - ERR
echo "Activated SmolVLA model ${VERSION}"