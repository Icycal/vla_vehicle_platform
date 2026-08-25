#!/usr/bin/env bash
set -eo pipefail

OPT_ROOT="${VLA_OPT_ROOT:-/opt/vla-vehicle}"
ETC_ROOT="${VLA_ETC_ROOT:-/etc/vla-vehicle}"
VAR_ROOT="${VLA_VAR_ROOT:-/var/lib/vla-vehicle}"
LOG_ROOT="${VLA_LOG_ROOT:-/var/log/vla-vehicle}"
SYSTEMD_ROOT="${VLA_SYSTEMD_ROOT:-/etc/systemd/system}"
SKIP_SYSTEMD="${VLA_SKIP_SYSTEMD:-0}"
SERVICE_USER="${VLA_SERVICE_USER:-${SUDO_USER:-}}"
SERVICE_GROUP="${VLA_SERVICE_GROUP:-}"
ROS_GROUPS="${VLA_ROS_GROUPS:-video dialout}"
POLICY_GROUPS="${VLA_POLICY_GROUPS:-docker}"

archive=""
policy_image_archive=""
activate=false
enable_services=false

usage() {
  cat <<'EOF'
Usage: install_release.sh --archive FILE [options]

Options:
  --policy-image FILE    Load the exported Policy Runtime OCI image.
  --activate             Activate the installed version.
  --enable-services      Enable and start production services.
  --no-systemd           Do not install or reload systemd units.
  --service-user USER    User that runs production services.
  --service-group GROUP  Primary group for production services.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --archive) archive="${2:?}"; shift 2 ;;
    --policy-image) policy_image_archive="${2:?}"; shift 2 ;;
    --activate) activate=true; shift ;;
    --enable-services) enable_services=true; activate=true; shift ;;
    --no-systemd) SKIP_SYSTEMD=1; shift ;;
    --service-user) SERVICE_USER="${2:?}"; shift 2 ;;
    --service-group) SERVICE_GROUP="${2:?}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "${archive}" || ! -f "${archive}" ]]; then
  echo "Release archive not found: ${archive}" >&2
  exit 2
fi

archive="$(readlink -f "${archive}")"
if [[ -n "${policy_image_archive}" ]]; then
  if [[ ! -f "${policy_image_archive}" ]]; then
    echo "Policy image archive not found: ${policy_image_archive}" >&2
    exit 2
  fi
  policy_image_archive="$(readlink -f "${policy_image_archive}")"
fi

if [[ "${OPT_ROOT}" == "/opt/vla-vehicle" && "$(id -u)" -ne 0 ]]; then
  echo "Production installation requires root. Run with sudo." >&2
  exit 1
fi

if [[ -n "${SERVICE_USER}" ]]; then
  if ! id "${SERVICE_USER}" >/dev/null 2>&1; then
    echo "A valid service user is required. Use --service-user or VLA_SERVICE_USER." >&2
    exit 1
  fi
  if [[ -z "${SERVICE_GROUP}" ]]; then
    SERVICE_GROUP="$(id -gn "${SERVICE_USER}")"
  fi
  if ! getent group "${SERVICE_GROUP}" >/dev/null 2>&1; then
    echo "Service group does not exist: ${SERVICE_GROUP}" >&2
    exit 1
  fi
fi
if [[ "${SKIP_SYSTEMD}" != "1" && -z "${SERVICE_USER}" ]]; then
  echo "A valid service user is required. Use --service-user or VLA_SERVICE_USER." >&2
  exit 1
fi
if [[ "${SKIP_SYSTEMD}" == "1" && "${enable_services}" == "true" ]]; then
  echo "--enable-services cannot be combined with --no-systemd." >&2
  exit 2
fi

existing_groups() {
  local configured_groups="$1"
  local result=()
  local group
  for group in ${configured_groups}; do
    if getent group "${group}" >/dev/null 2>&1; then
      result+=("${group}")
    fi
  done
  printf '%s' "${result[*]}"
}

ROS_GROUPS="$(existing_groups "${ROS_GROUPS}")"
POLICY_GROUPS="$(existing_groups "${POLICY_GROUPS}")"

verify_sidecar() {
  local file="$1"
  local sidecar="${file}.sha256"
  if [[ -f "${sidecar}" ]]; then
    (
      cd "$(dirname "${file}")"
      sha256sum --check "$(basename "${sidecar}")"
    )
  fi
}

escape_sed_replacement() {
  printf '%s' "$1" | sed 's/[\\&|]/\\&/g'
}

verify_sidecar "${archive}"
if [[ -n "${policy_image_archive}" ]]; then
  verify_sidecar "${policy_image_archive}"
fi

archive_root="$(
  tar --zstd -tf "${archive}" |
    python3 -c '
import re
import sys

root = None
for raw_name in sys.stdin:
    name = raw_name.rstrip("\n")
    if not name or name.startswith("/") or "\\" in name:
        raise SystemExit(f"Unsafe archive member: {name!r}")
    parts = [part for part in name.split("/") if part not in ("", ".")]
    if not parts or ".." in parts:
        raise SystemExit(f"Unsafe archive member: {name!r}")
    if root is None:
        root = parts[0]
    elif parts[0] != root:
        raise SystemExit("Release archive contains multiple roots")

if root is None or re.fullmatch(r"vla-vehicle-[A-Za-z0-9._+-]+", root) is None:
    raise SystemExit(f"Invalid release archive root: {root!r}")
print(root)
'
)"

mkdir -p "${OPT_ROOT}/releases" "${ETC_ROOT}" "${LOG_ROOT}" \
  "${VAR_ROOT}/policy" "${VAR_ROOT}/datasets/episodes" "${VAR_ROOT}/recordings"

staging="$(mktemp -d "${OPT_ROOT}/releases/.install-XXXXXX")"
cleanup() {
  if [[ -d "${staging}" ]]; then
    case "${staging}" in
      "${OPT_ROOT}/releases/.install-"*) rm -rf "${staging}" ;;
      *) echo "Refusing to clean unexpected staging path: ${staging}" >&2 ;;
    esac
  fi
}
trap cleanup EXIT

tar --zstd --no-same-owner --no-same-permissions -xf "${archive}" -C "${staging}"
mapfile -t roots < <(find "${staging}" -mindepth 1 -maxdepth 1 -type d)
if [[ "${#roots[@]}" -ne 1 || ! -f "${roots[0]}/manifest.json" ]]; then
  echo "Release archive must contain one bundle root with manifest.json." >&2
  exit 1
fi

bundle_root="${roots[0]}"
if [[ "$(basename "${bundle_root}")" != "${archive_root}" ]]; then
  echo "Extracted release root does not match archive listing." >&2
  exit 1
fi

special_file="$(find "${bundle_root}" -xdev ! -type d ! -type f ! -type l -print -quit)"
if [[ -n "${special_file}" ]]; then
  echo "Release archive contains an unsupported special file: ${special_file}" >&2
  exit 1
fi

while IFS= read -r -d '' link_path; do
  resolved_link="$(readlink -f "${link_path}" || true)"
  case "${resolved_link}" in
    "${bundle_root}"/*) ;;
    *) echo "Release symlink escapes the bundle: ${link_path}" >&2; exit 1 ;;
  esac
done < <(find "${bundle_root}" -type l -print0)

mapfile -t manifest_fields < <(
  python3 - "${bundle_root}/manifest.json" <<'PY'
import json
import sys

manifest = json.load(open(sys.argv[1], encoding='utf-8'))
for key in ('schema_version', 'release_version', 'architecture', 'operation_mode', 'policy_provider', 'policy_image'):
    value = manifest.get(key)
    if not isinstance(value, str):
        raise SystemExit(f"Invalid manifest field: {key}")
    print(value)
PY
)
schema_version="${manifest_fields[0]}"
version="${manifest_fields[1]}"
architecture="${manifest_fields[2]}"
operation_mode="${manifest_fields[3]}"
policy_provider="${manifest_fields[4]}"
policy_image="${manifest_fields[5]}"

if [[ "${schema_version}" != "vla.release.v1" || ! "${version}" =~ ^[A-Za-z0-9._+-]+$ ]]; then
  echo "Invalid release manifest identity." >&2
  exit 1
fi
if [[ "${archive_root}" != "vla-vehicle-${version}" ]]; then
  echo "Archive root and manifest version do not match." >&2
  exit 1
fi
if [[ "${operation_mode}" != "shadow" || "${policy_provider}" != "mock" ]]; then
  echo "This installer accepts only the current Shadow/Mock production profile." >&2
  exit 1
fi

if [[ "${architecture}" != "$(uname -m)" ]]; then
  echo "Architecture mismatch: release=${architecture} host=$(uname -m)" >&2
  exit 1
fi

(
  cd "${bundle_root}"
  sha256sum --check --quiet checksums.sha256
)

release_dir="${OPT_ROOT}/releases/${version}"
if [[ -e "${release_dir}" ]]; then
  echo "Release is already installed: ${release_dir}" >&2
  exit 1
fi

mv "${bundle_root}" "${release_dir}"

etc_root_escaped="$(escape_sed_replacement "${ETC_ROOT}")"
var_root_escaped="$(escape_sed_replacement "${VAR_ROOT}")"
log_root_escaped="$(escape_sed_replacement "${LOG_ROOT}")"
for default_file in "${release_dir}/config/defaults/"*; do
  file_name="$(basename "${default_file}")"
  sed \
    -e "s|@VLA_ETC_ROOT@|${etc_root_escaped}|g" \
    -e "s|@VLA_VAR_ROOT@|${var_root_escaped}|g" \
    -e "s|@VLA_LOG_ROOT@|${log_root_escaped}|g" \
    "${default_file}" > "${ETC_ROOT}/${file_name}.dist"
  if [[ ! -e "${ETC_ROOT}/${file_name}" ]]; then
    cp -a "${ETC_ROOT}/${file_name}.dist" "${ETC_ROOT}/${file_name}"
  fi
done

if [[ "$(id -u)" -eq 0 && -n "${SERVICE_USER}" ]]; then
  chown -R "${SERVICE_USER}:${SERVICE_GROUP}" "${VAR_ROOT}" "${LOG_ROOT}"
  chown root:root "${ETC_ROOT}"/* 2>/dev/null || true
  chmod -R go-w "${release_dir}"
fi

if [[ "${SKIP_SYSTEMD}" != "1" ]]; then
  opt_root_escaped="$(escape_sed_replacement "${OPT_ROOT}")"
  service_user_escaped="$(escape_sed_replacement "${SERVICE_USER}")"
  service_group_escaped="$(escape_sed_replacement "${SERVICE_GROUP}")"
  ros_groups_escaped="$(escape_sed_replacement "${ROS_GROUPS}")"
  policy_groups_escaped="$(escape_sed_replacement "${POLICY_GROUPS}")"
  for template in "${release_dir}/systemd/"*.service; do
    unit_path="${SYSTEMD_ROOT}/$(basename "${template}")"
    sed \
      -e "s|@VLA_OPT_ROOT@|${opt_root_escaped}|g" \
      -e "s|@VLA_ETC_ROOT@|${etc_root_escaped}|g" \
      -e "s|@VLA_VAR_ROOT@|${var_root_escaped}|g" \
      -e "s|@VLA_SERVICE_USER@|${service_user_escaped}|g" \
      -e "s|@VLA_SERVICE_GROUP@|${service_group_escaped}|g" \
      -e "s|@VLA_ROS_GROUPS@|${ros_groups_escaped}|g" \
      -e "s|@VLA_POLICY_GROUPS@|${policy_groups_escaped}|g" \
      "${template}" > "${unit_path}"
    chmod 0644 "${unit_path}"
  done
  systemctl daemon-reload
fi

if [[ -n "${policy_image_archive}" ]]; then
  zstd -d -c "${policy_image_archive}" | docker load
fi

if $activate && [[ "${SKIP_SYSTEMD}" != "1" ]]; then
  if ! docker image inspect "${policy_image}" >/dev/null 2>&1; then
    echo "Required Policy Runtime image is unavailable: ${policy_image}" >&2
    exit 1
  fi
fi

if $activate; then
  VLA_OPT_ROOT="${OPT_ROOT}" \
  VLA_ETC_ROOT="${ETC_ROOT}" \
  VLA_VAR_ROOT="${VAR_ROOT}" \
  VLA_SKIP_SYSTEMD="${SKIP_SYSTEMD}" \
    "${release_dir}/admin/activate_release.sh" "${version}"
fi

if $enable_services; then
  systemctl enable --now vla-policy-runtime.service vla-ros-shadow.service
fi

trap - EXIT
cleanup

echo "Installed VLA release ${version} at ${release_dir}"
