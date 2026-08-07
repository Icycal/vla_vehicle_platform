#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$({ cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd; })"
PROFILE_PATH="${1:?training profile JSON is required}"
DATASET_PATH="${2:?LeRobot dataset directory is required}"
OUTPUT_PATH="${3:?training output directory is required}"
shift 3
EXTRA_TRAIN_ARGUMENTS=("$@")

PROFILE_PATH="$(realpath "${PROFILE_PATH}")"
DATASET_PATH="$(realpath "${DATASET_PATH}")"
OUTPUT_PATH="$(realpath -m "${OUTPUT_PATH}")"
OUTPUT_PARENT="$(dirname "${OUTPUT_PATH}")"
OUTPUT_NAME="$(basename "${OUTPUT_PATH}")"
BASE_MODEL_DIR="$(realpath "${SMOLVLA_BASE_MODEL_DIR:-${PROJECT_ROOT}/run/models/smolvla_base}")"
HF_HOME_DIR="$(realpath "${SMOLVLA_HF_HOME:-${PROJECT_ROOT}/run/models/huggingface}")"
VLM_MANIFEST="$(realpath "${SMOLVLA_VLM_MANIFEST:-${HF_HOME_DIR}/smolvlm-manifest.json}")"

if [[ -e "${OUTPUT_PATH}" ]]; then
  echo "Output path already exists: ${OUTPUT_PATH}" >&2
  exit 2
fi
mkdir -p "${OUTPUT_PARENT}" "${PROJECT_ROOT}/run/training/logs" "${PROJECT_ROOT}/run/training/overlays"
OVERLAY_WORK_DIR="$(mktemp -d "${PROJECT_ROOT}/run/training/overlays/${OUTPUT_NAME}.XXXXXX")"
OVERLAY_DIR="${OVERLAY_WORK_DIR}/policy"
LOG_FILE="${PROJECT_ROOT}/run/training/logs/${OUTPUT_NAME}.log"
RESOURCE_LOG="${PROJECT_ROOT}/run/training/logs/${OUTPUT_NAME}.tegrastats.log"
STARTED_AT="$(date --utc +%Y-%m-%dT%H:%M:%SZ)"
GIT_COMMIT="$(git -C "${PROJECT_ROOT}" rev-parse HEAD)"
MONITOR_PID=""

stop_monitor() {
  if [[ -n "${MONITOR_PID}" ]] && kill -0 "${MONITOR_PID}" 2>/dev/null; then
    kill "${MONITOR_PID}" 2>/dev/null || true
    wait "${MONITOR_PID}" 2>/dev/null || true
  fi
  MONITOR_PID=""
}

cleanup() {
  stop_monitor
  rm -rf -- "${OVERLAY_WORK_DIR}"
}
trap cleanup EXIT INT TERM

python3 "${PROJECT_ROOT}/training/scripts/prepare_policy_overlay.py" \
  --profile "${PROFILE_PATH}" \
  --dataset "${DATASET_PATH}" \
  --base-model "${BASE_MODEL_DIR}" \
  --vlm-manifest "${VLM_MANIFEST}" \
  --hf-home "${HF_HOME_DIR}" \
  --output "${OVERLAY_DIR}"

mapfile -t runtime_values < <(
  python3 - "${OVERLAY_DIR}/runtime.json" <<'PY'
import json
import sys
runtime = json.load(open(sys.argv[1], encoding="utf-8"))
profile = runtime["profile"]
values = [
    profile["profile_id"],
    profile["platform"],
    profile["image"],
    profile["batch_size"],
    profile["steps"],
    profile["num_workers"],
    profile["eval_freq"],
    profile["log_freq"],
    profile["save_checkpoint"],
    profile["save_freq"],
    profile["seed"],
    profile["image_transforms"],
    profile["monitor_tegrastats"],
    runtime["dataset_repo_id"],
    runtime["host_vlm_cache"],
]
for value in values:
    print(str(value).lower() if isinstance(value, bool) else value)
PY
)

PROFILE_ID="${runtime_values[0]}"
PLATFORM="${runtime_values[1]}"
IMAGE="${TRAINING_IMAGE:-${runtime_values[2]}}"
BATCH_SIZE="${TRAINING_BATCH_SIZE:-${runtime_values[3]}}"
STEPS="${TRAINING_STEPS:-${runtime_values[4]}}"
NUM_WORKERS="${TRAINING_NUM_WORKERS:-${runtime_values[5]}}"
EVAL_FREQ="${runtime_values[6]}"
LOG_FREQ="${runtime_values[7]}"
SAVE_CHECKPOINT="${TRAINING_SAVE_CHECKPOINT:-${runtime_values[8]}}"
SAVE_FREQ="${runtime_values[9]}"
SEED="${runtime_values[10]}"
IMAGE_TRANSFORMS="${runtime_values[11]}"
MONITOR_TEGRASTATS="${runtime_values[12]}"
DATASET_REPO_ID="${runtime_values[13]}"
VLM_CACHE_DIR="${runtime_values[14]}"
JOB_NAME="${TRAINING_JOB_NAME:-${OUTPUT_NAME}}"
WANDB_ENABLE="${TRAINING_WANDB_ENABLE:-false}"

if [[ "${MONITOR_TEGRASTATS}" == "true" ]] && command -v tegrastats >/dev/null 2>&1; then
  tegrastats --interval 1000 >"${RESOURCE_LOG}" 2>&1 &
  MONITOR_PID=$!
fi

docker_arguments=(
  run --rm
  --network none
  --ipc host
  --user "$(id -u):$(id -g)"
  -e HOME=/tmp
  -e HF_HOME=/tmp/huggingface
  -e HF_HUB_OFFLINE=1
  -e TRANSFORMERS_OFFLINE=1
  -e HF_DATASETS_OFFLINE=1
  -e TOKENIZERS_PARALLELISM=false
  -e NVIDIA_VISIBLE_DEVICES=all
  -e NVIDIA_DRIVER_CAPABILITIES=compute,utility
  -v "${DATASET_PATH}:/datasets/${DATASET_REPO_ID}:ro"
  -v "${BASE_MODEL_DIR}:/models/smolvla_base:ro"
  -v "${OVERLAY_DIR}/config.json:/models/smolvla_base/config.json:ro"
  -v "${OVERLAY_DIR}/policy_preprocessor.json:/models/smolvla_base/policy_preprocessor.json:ro"
  -v "${VLM_CACHE_DIR}:/models/smolvlm-cache:ro"
  -v "${PROJECT_ROOT}/training/scripts/lerobot_train_compat.py:/opt/vla-training/lerobot_train_compat.py:ro"
  -v "${OUTPUT_PARENT}:/output"
)
if [[ "${PLATFORM}" == "orin" ]]; then
  docker_arguments+=( --runtime nvidia )
else
  docker_arguments+=( --gpus all )
fi

train_arguments=(
  python3 /opt/vla-training/lerobot_train_compat.py
  "--dataset.repo_id=${DATASET_REPO_ID}"
  "--dataset.root=/datasets/${DATASET_REPO_ID}"
  "--dataset.image_transforms.enable=${IMAGE_TRANSFORMS}"
  --policy.path=/models/smolvla_base
  "--output_dir=/output/${OUTPUT_NAME}"
  "--job_name=${JOB_NAME}"
  "--batch_size=${BATCH_SIZE}"
  "--steps=${STEPS}"
  "--num_workers=${NUM_WORKERS}"
  "--eval_freq=${EVAL_FREQ}"
  "--log_freq=${LOG_FREQ}"
  "--save_checkpoint=${SAVE_CHECKPOINT}"
  "--save_freq=${SAVE_FREQ}"
  "--seed=${SEED}"
  "--wandb.enable=${WANDB_ENABLE}"
)
train_arguments+=("${EXTRA_TRAIN_ARGUMENTS[@]}")

printf 'profile=%s\nimage=%s\ndataset=%s\noutput=%s\nbatch_size=%s\nsteps=%s\n' \
  "${PROFILE_ID}" "${IMAGE}" "${DATASET_REPO_ID}" "${OUTPUT_PATH}" "${BATCH_SIZE}" "${STEPS}"

set +e
docker "${docker_arguments[@]}" "${IMAGE}" "${train_arguments[@]}" 2>&1 | tee "${LOG_FILE}"
TRAIN_STATUS=${PIPESTATUS[0]}
set -e
stop_monitor

mkdir -p "${OUTPUT_PATH}/training_overlay"
cp "${PROFILE_PATH}" "${OUTPUT_PATH}/training_overlay/profile.json"
cp "${OVERLAY_DIR}/config.json" "${OUTPUT_PATH}/training_overlay/config.json"
cp "${OVERLAY_DIR}/policy_preprocessor.json" "${OUTPUT_PATH}/training_overlay/policy_preprocessor.json"
cp "${OVERLAY_DIR}/runtime.json" "${OUTPUT_PATH}/training_overlay/runtime.json"
cp "${LOG_FILE}" "${OUTPUT_PATH}/training.log"
if [[ -f "${RESOURCE_LOG}" ]]; then
  cp "${RESOURCE_LOG}" "${OUTPUT_PATH}/tegrastats.log"
fi

python3 "${PROJECT_ROOT}/training/scripts/write_training_manifest.py" \
  --runtime "${OVERLAY_DIR}/runtime.json" \
  --dataset "${DATASET_PATH}" \
  --base-model "${BASE_MODEL_DIR}" \
  --output "${OUTPUT_PATH}" \
  --log "${LOG_FILE}" \
  --resource-log "${RESOURCE_LOG}" \
  --status "${TRAIN_STATUS}" \
  --git-commit "${GIT_COMMIT}" \
  --image "${IMAGE}" \
  --started-at "${STARTED_AT}"

if [[ ${TRAIN_STATUS} -ne 0 ]]; then
  echo "Training failed with exit code ${TRAIN_STATUS}; diagnostics retained in ${OUTPUT_PATH}" >&2
  exit "${TRAIN_STATUS}"
fi

echo "SMOLVLA_TRAINING_PASS"