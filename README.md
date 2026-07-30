# VLA Vehicle Platform

Greenfield ROS 2 Humble platform for the Jetson Orin NX Ackermann vehicle. Project-owned ROS nodes
use C++17, `rclcpp`, and `ament_cmake`. Legacy workspaces remain read-only references.

## Git workspace

The primary Git working tree is `/home/wheeltec/vla_vehicle_platform` on the Orin NX. Workstation
copies are staging, backup, or deployment mirrors and must not develop an independent history.
Build outputs, generated vendor links, runtime logs, model weights, datasets, and credentials are
excluded from Git.

## Layout

- `ros_ws/src/vehicle_interfaces`: versioned ROS messages, services, and actions.
- `ros_ws/src/vehicle_runtime`: supervisor, policy gateway, action runtime, control mux, and safety guard.
- `ros_ws/src/vehicle_policy_transport`: in-process Mock and Protobuf Unix Socket policy transports.
- `ros_ws/src/vehicle_bringup`: XML launch files and YAML parameters.
- `policy-runtime`: non-ROS Python provider router and isolated Policy Runtime server.
- `protocol`: model-independent Policy Runtime Protobuf contract.
- `third_party/wheeltec_ros2`: imported Nav2, chassis, message, and serial source snapshots.
- `scripts`: reproducible workspace preparation, build, launch, and smoke checks.

## Phase 0 build

```bash
cd /home/wheeltec/vla_vehicle_platform
chmod +x scripts/*.sh
./scripts/build.sh
./scripts/run_phase0.sh
```

Phase 0 launches only the new zero-motion control chain. It does not launch
`wheeltec_robot_node`, so it cannot intentionally command the STM32 chassis.

Build the imported chassis package separately after Phase 0 validation:

```bash
./scripts/build_chassis.sh
```

This build validates source compatibility only. Starting `wheeltec_robot_node` is a separate,
explicit hardware-in-the-loop step and is intentionally outside Phase 0.

## Phase 1 Shadow data

Phase 1 starts the front USB camera, observation health monitor, episode recorder, and the Phase 0
zero-motion chain. It still does not launch the chassis driver.

```bash
./scripts/build.sh
./scripts/run_phase1_shadow.sh
./scripts/check_phase1_shadow.sh
./scripts/check_shadow_contract.sh
./scripts/start_episode.sh episode-test "front camera validation" wheeltec
./scripts/stop_episode.sh true "validation complete"
```

Episodes are stored under `datasets/episodes` as rosbag2 data plus an
`episode_manifest.json`. The current camera calibration file is a placeholder; complete an actual
checkerboard calibration before enabling `require_calibration` or using images for geometric tasks.
See `docs/CAMERA_CALIBRATION.md` for the acceptance checklist.

The model-independent Observation contract and current state keys are documented in
`docs/OBSERVATION_CONTRACT.md`. `PolicyAction.observation_id` provides traceability from the input
image/state/task bundle through prediction and Shadow evaluation.

## External Policy Runtime

The default remains the in-process C++ Mock transport. To validate the isolated runtime without
installing ROS inside it:

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/run_mock_policy_runtime.sh
```

In another terminal, start Shadow mode with the Unix Socket override:

```bash
./scripts/run_phase1_shadow.sh \
  policy_params_file:=/home/wheeltec/vla_vehicle_platform/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/policy_socket.yaml
```

The Policy Runtime exchanges length-prefixed Protobuf envelopes over
`run/policy/policy.sock`. It has no ROS dependency and cannot publish `/cmd_vel`. See
`docs/POLICY_RUNTIME.md` for protocol, provider, container, and failure behavior.

The verified Jetson compute stack and the acceptance gates for the later NVIDIA SmolVLA image are
recorded in `docs/SMOLVLA_PLATFORM_READINESS.md`. Refresh the inventory with
`scripts/inspect_smolvla_platform.sh` after JetPack or CUDA changes.

Build and repeat the validated CUDA/PyTorch GPU test with `scripts/build_pytorch_smoke.sh` and
`scripts/run_pytorch_smoke.sh`. The test scope and acceptance criteria are documented in
`docs/PYTORCH_SMOKE.md`.

Do not source either legacy workspace before building or running this project. See
`DEPENDENCIES.md` for source provenance and licensing constraints.
