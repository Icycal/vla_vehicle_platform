#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
container_name=vla-smolvla-runtime

for _ in $(seq 1 180); do
  health="$(docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{end}}' "${container_name}" 2>/dev/null || true)"
  if [[ "${health}" == "healthy" ]]; then
    break
  fi
  if [[ "$(docker inspect --format '{{.State.Status}}' "${container_name}" 2>/dev/null || true)" == "exited" ]]; then
    docker logs "${container_name}" >&2 || true
    exit 1
  fi
  sleep 1
done

if [[ "${health:-}" != "healthy" ]]; then
  docker logs "${container_name}" >&2 || true
  echo "SmolVLA Runtime did not become healthy." >&2
  exit 1
fi

docker exec   -e SMOLVLA_SMOKE_TIMEOUT=30   "${container_name}"   python3 -m runtime.smoke_client

docker logs --tail 40 "${container_name}"
echo SMOLVLA_RUNTIME_CONTAINER_PASS
