# SmolVLA Platform Readiness

Snapshot date: 2026-07-30

## Hardware and Operating System

- Platform: NVIDIA Jetson Orin NX Engineering Reference Developer Kit Super.
- Architecture: `aarch64`.
- Kernel: `5.15.148-tegra`.
- L4T: release `R36`, revision `4.3`.
- L4T core and CUDA packages: `36.4.3-20250107174145`.
- Power mode: `MAXN_SUPER`.

## Compute Stack

- CUDA toolkit: `12.6`, compiler `V12.6.68` at `/usr/local/cuda/bin/nvcc`.
- The CUDA compiler directory is not currently present in the interactive shell `PATH`.
- cuDNN: `9.3.0.75` for CUDA 12.
- TensorRT: `10.3.0.30` for CUDA 12.5-compatible packaging.
- Host Python: `3.10.12`.
- Host Python does not contain PyTorch, which preserves the container isolation boundary.
- The `nvidia-jetpack` meta-package is not installed; compatibility decisions must use the installed
  L4T/CUDA package versions rather than assuming a meta-package version.

## Capacity

- Physical memory: approximately 15 GiB total and 12 GiB available during inspection.
- Swap: approximately 7.6 GiB and unused during inspection.
- NVMe root filesystem: 233 GiB total, 124 GiB available.
- Front camera: MJPEG, 640 x 480 in the active configuration.

These values are sufficient to begin container compatibility and model-loading experiments, but
actual SmolVLA memory, latency, temperature, and throttling behavior must be measured with the
selected model and precision.

## Container Runtime

- Docker Engine: `28.5.1`.
- NVIDIA Container Toolkit: `1.16.2-1`.
- Docker runtimes include `nvidia`; the default runtime remains `runc`.
- A plain ARM64 Python container launched with `--runtime nvidia` and
  `NVIDIA_VISIBLE_DEVICES=all` receives the Jetson GPU device nodes and the driver library under
  `/usr/lib/aarch64-linux-gnu/nvidia`.
- The plain `python:3.10-slim` image does not provide the complete CUDA, PyTorch, torchvision, and
  LeRobot stack. It is therefore suitable only for the Mock Runtime, not for SmolVLA inference.

## Container Shadow Validation

The Docker Mock Runtime completed the full Phase 1 Shadow path:

- Policy status reported `mock-runtime` and `mock-runtime-zero-policy-v1`.
- Gateway inference transport latency was approximately 0.9 to 1.8 ms during checks.
- Shadow linear and angular MAE remained zero.
- Final `/cmd_vel` remained zero.
- `wheeltec_robot_node` was not launched.
- Episode `container-shadow-001` recorded 50 Observations, 49 Policy Actions, 49 Shadow
  Comparisons, 49 Shadow Metrics, and 36 compressed images.

Fault validation covered graceful stop, process freeze, and forced `SIGKILL` with a stale Socket.
The Gateway entered `STATE_ERROR`, suppressed invalid actions, retained zero final command, and
automatically recovered to `STATE_READY` after the Runtime returned. The Runtime removes stale
Socket files before binding.

## CUDA PyTorch Smoke Validation

The ARM64 NVIDIA PyTorch `25.05-py3-igpu` base completed both 15-second and 60-second Smoke runs:

- PyTorch: `2.8.0a0+5228986c39.nv25.05`.
- Container CUDA: `12.9`.
- cuDNN: `9.10.1`.
- Device: Orin, compute capability `8.7`, approximately 15.29 GiB unified memory.
- Sixty-second run: 31,290 FP16 matrix iterations in 61.312 seconds.
- Matrix and cuDNN convolution outputs remained finite.
- Peak CUDA allocation: approximately 89.63 MiB.
- Tegrastats: 66 of 74 samples had active GR3D and 61 samples were at or above 90 percent.
- Peak GPU temperature: approximately 74.8 degrees Celsius.
- Peak junction temperature: approximately 75.2 degrees Celsius.
- Peak input power: approximately 35.3 W.
- Maximum recorded RAM use: approximately 5.5 GiB; maximum Swap use: 7 MiB.
- Smoke log contained no error, failure, illegal instruction, out-of-memory, assertion, or
  segmentation-fault line.

The CUDA/PyTorch base compatibility gate is complete. See `docs/PYTORCH_SMOKE.md` for the
reproducible build and execution procedure.

## Next Compatibility Gate

Before implementing `SmolVLAProvider`, the next image must add LeRobot without replacing the
validated Jetson-specific PyTorch installation. It must prove all of the following on the vehicle:

1. LeRobot and the selected SmolVLA revision install without replacing NVIDIA PyTorch.
2. Existing CUDA Smoke still passes after dependency installation.
3. Model weights load from the project model volume without requiring root.
4. A single offline observation produces a valid action chunk.
5. Idle and warm inference stay within memory and thermal limits.

Run `scripts/inspect_smolvla_platform.sh` to refresh this inventory after system updates.
