# CUDA PyTorch Smoke

## Purpose

The Smoke image validates the Jetson GPU container boundary before LeRobot or SmolVLA is added. It
tests ARM64 image selection, NVIDIA Runtime device injection, CUDA-enabled PyTorch, FP16 matrix
operations, cuDNN convolution, finite outputs, sustained execution, memory use, temperature, and
power behavior.

It contains no ROS dependency, model weights, LeRobot, or vehicle control path.

## Base Image

The validated local base tag is:

```text
vla-pytorch-base:25.05-igpu
```

It was imported from the NVIDIA PyTorch `25.05-py3-igpu` ARM64 image through a domestic registry
mirror. The repository uses the local tag so later builds do not depend on the acquisition source.

## Build

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/build_pytorch_smoke.sh
```

The build refuses a non-ARM64 base image, disables remote pulls, and uses host networking to avoid
the Jetson Docker bridge `raw` table limitation.

## Run

```bash
./scripts/run_pytorch_smoke.sh 60
```

The runner temporarily stops the Mock Policy Runtime when it is active, records `tegrastats`, runs
the Smoke image with NVIDIA Runtime and no container network, validates the pass marker, and
restores the Mock Runtime on exit.

Logs are written under `run/test` and are excluded from Git.

## Acceptance

- Image architecture is `arm64/linux`.
- `torch.cuda.is_available()` is true.
- One Orin CUDA device is visible.
- FP16 matrix and convolution outputs are finite.
- The process exits zero and prints `CUDA_PYTORCH_SMOKE_PASS`.
- Tegrastats shows active GR3D samples without runaway memory, Swap, temperature, or errors.

Passing this test confirms the base CUDA/PyTorch container. It does not confirm SmolVLA model
compatibility, model memory use, inference latency, or long-duration thermal behavior.
