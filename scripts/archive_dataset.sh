#!/usr/bin/env bash
set -euo pipefail

OUTPUT_PATH="${1:?output zip path is required}"
shift
if [[ $# -eq 0 ]]; then
  echo "At least one dataset directory is required" >&2
  exit 2
fi

OUTPUT_PATH="$(realpath -m "${OUTPUT_PATH}")"
mkdir -p "$(dirname "${OUTPUT_PATH}")"
TEMP_PATH="${OUTPUT_PATH}.partial"
rm -f "${TEMP_PATH}"
INPUTS=()
for input_path in "$@"; do
  INPUT_PATH="$(realpath "${input_path}")"
  if [[ ! -d "${INPUT_PATH}" ]]; then
    echo "Dataset directory does not exist: ${INPUT_PATH}" >&2
    exit 2
  fi
  INPUTS+=("${INPUT_PATH}")
done

STAGING="$(mktemp -d)"
cleanup() { rm -rf "${STAGING}"; rm -f "${TEMP_PATH}"; }
trap cleanup EXIT

for input_path in "${INPUTS[@]}"; do
  cp -a "${input_path}" "${STAGING}/"
done
(
  cd "${STAGING}"
  zip -qr "${TEMP_PATH}" .
)
mv -f "${TEMP_PATH}" "${OUTPUT_PATH}"
trap - EXIT
rm -rf "${STAGING}"
echo "Created archive: ${OUTPUT_PATH}"