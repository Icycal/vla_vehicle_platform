#!/usr/bin/env bash

vla_machine_arch() {
  uname -m
}

vla_artifact_arch() {
  case "${1:-$(vla_machine_arch)}" in
    aarch64) printf '%s\n' arm64 ;;
    x86_64) printf '%s\n' amd64 ;;
    *) printf '%s\n' "${1:-$(vla_machine_arch)}" ;;
  esac
}

vla_platform_profile() {
  if [[ -r /etc/nv_tegra_release ]]; then
    printf '%s\n' jetson
  else
    printf '%s\n' generic-linux
  fi
}
