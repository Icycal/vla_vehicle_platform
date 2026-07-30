#!/usr/bin/env bash
set -euo pipefail

show_packages() {
  dpkg-query --showformat='${Package} ${Version}\n' --show "$@" 2>/dev/null || true
}

echo "=== Platform ==="
date --iso-8601=seconds
uname -a
cat /etc/nv_tegra_release 2>/dev/null || true
tr -d '\0' </proc/device-tree/model 2>/dev/null || true
echo

echo "=== JetPack and L4T ==="
dpkg-query --show nvidia-jetpack 2>/dev/null || echo "nvidia-jetpack meta-package not installed"
show_packages nvidia-l4t-core nvidia-l4t-cuda 'nvidia-l4t-container*'

echo "=== CUDA ==="
if [[ -x /usr/local/cuda/bin/nvcc ]]; then
  /usr/local/cuda/bin/nvcc --version
else
  echo "/usr/local/cuda/bin/nvcc not installed"
fi
ls -ld /usr/local/cuda* 2>/dev/null || true

echo "=== cuDNN and TensorRT ==="
show_packages 'libcudnn*' 'libnvinfer*'

echo "=== Capacity ==="
free -h
df -h / /home /var/lib/docker
nvpmodel -q 2>&1 || true

echo "=== Docker and NVIDIA Runtime ==="
docker info 2>/dev/null | grep -E 'Server Version|Runtimes|Default Runtime|Docker Root Dir' || true
show_packages 'nvidia-container*'
docker run --rm --runtime nvidia --network none \
  --env NVIDIA_VISIBLE_DEVICES=all python:3.10-slim \
  ls -l /dev/nvhost-gpu /dev/nvmap /dev/nvhost-ctrl-gpu

echo "=== Host Python ==="
python3 --version
python3 - <<'PY'
try:
    import torch
    print("torch", torch.__version__)
    print("cuda_available", torch.cuda.is_available())
except Exception as error:
    print("host_torch_unavailable", error)
PY

echo "=== Front Camera ==="
v4l2-ctl -d /dev/video0 --get-fmt-video 2>/dev/null || true
