# 赤兔（Chitu）Vehicle Intelligence Platform

<p align="center"><img src="docs/assets/chitu-logo-preview.png" alt="赤兔 Chitu" width="640"></p>

Greenfield ROS 2 Humble platform for the Jetson Orin NX Ackermann vehicle. Project-owned ROS nodes
use C++17, `rclcpp`, and `ament_cmake`. Legacy workspaces remain read-only references.

## Git workspace

The primary Git working tree is `/home/wheeltec/vla_vehicle_platform` on the Orin NX. Workstation
copies are staging, backup, or deployment mirrors and must not develop an independent history.
Build outputs, generated vendor links, runtime logs, model weights, datasets, and credentials are
excluded from Git.

## ROS environment isolation

Do not rely on workspace `source` order in `.bashrc`. Open an isolated project shell with:

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/ros_env.sh vla
```

Use `wheeltec`, `autoware`, or `rhzd` instead of `vla` when working on a legacy stack. Project
build, launch, check, and episode scripts automatically re-enter the isolated `vla` profile. See
`docs/ROS_ENVIRONMENTS.md` for profile composition, ROS Domain defaults, command mode, and optional
aliases.

## Relocatable development paths

Development scripts derive the repository root from their own location; the checkout does not need
to live under a particular user home directory. Runtime data locations can be overridden without
editing ROS YAML files:

```bash
export VLA_STATE_ROOT=/srv/vla/state
export VLA_RUNTIME_ROOT=/run/user/$(id -u)/vla-vehicle
export VLA_LOG_ROOT=/srv/vla/log
./scripts/run_phase1_shadow.sh
```

The derived defaults keep development data inside the checkout. `VLA_POLICY_SOCKET`,
`VLA_EPISODE_ROOT`, `VLA_DEBUG_ROOT`, and `VLA_JOBS_ROOT` can override individual locations.
`scripts/install_component_services.sh` renders user-systemd units with the current checkout and
runtime paths instead of assuming a fixed username or directory.

## Production releases

Production vehicles receive a merged native-architecture ROS install bundle and an exported OCI
Policy Runtime image instead of the Git repository. Build on the same CPU architecture as the
target vehicle; an Orin runner produces `arm64` artifacts and an x86_64 runner produces `amd64`
artifacts:

```bash
./scripts/build_release.sh --version 0.2.0
```

The default release includes the Wheeltec chassis package. Build a hardware-neutral platform
bundle when the chassis driver is supplied separately:

```bash
./scripts/build_release.sh --version 0.2.0 --chassis-provider none
```

Deploy it atomically to the vehicle after the one-time deployer bootstrap:

```bash
./scripts/deploy_release.sh \
  --target wheeltec@10.101.70.232 \
  --archive artifacts/releases/0.2.0/vla-vehicle-0.2.0-arm64.tar.zst \
  --policy-image artifacts/releases/0.2.0/vla-policy-runtime-0.2.0-arm64.oci.tar.zst \
  --non-interactive
```

See `docs/PRODUCTION_DEPLOYMENT.md` and `docs/ORIN_RELEASE_RUNNER.md`. The current production image
contains the Mock Provider and remains Shadow-only; it never starts `wheeltec_robot_node`.

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

## Front camera

The project uses the vendored C++ `usb_cam` driver and provides an independent camera Bringup so
camera publishing can be tested without starting the rest of Phase 1:

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/build.sh
./scripts/run_front_camera.sh
```

In another terminal:

```bash
./scripts/check_front_camera.sh
```

The default configuration opens `/dev/video0` as MJPEG at `640x480`, publishes raw images on
`/camera/image_raw`, camera calibration metadata on `/camera/camera_info`, and JPEG-compressed
images on `/camera/image_compressed`. `phase1_shadow.launch.xml` includes the same camera launch.
The current camera-info file remains an uncalibrated placeholder and must not be treated as valid
geometric calibration.

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

## SmolVLA image demo

The offline Demo loads the downloaded SmolVLA and SmolVLM2 snapshots, reads three image paths plus
a six-element test state, and prints the predicted `50 x 6` action chunk. Prepare the runtime-only
configuration and capture a frame from the current USB camera:

```bash
cd /home/wheeltec/vla_vehicle_platform
mkdir -p run/config run/test/smolvla-demo/images
cp config/smolvla-demo.env.example run/config/smolvla-demo.env

ffmpeg \
  -hide_banner \
  -loglevel warning \
  -f v4l2 \
  -input_format mjpeg \
  -video_size 640x480 \
  -framerate 30 \
  -i /dev/video0 \
  -frames:v 1 \
  -y \
  run/test/smolvla-demo/images/front.jpg

./scripts/run_smolvla_demo.sh
```

The one-shot container exits and is removed after inference. Its log remains at
`run/test/smolvla-demo/logs/image-inference.log`. The example maps the same physical camera frame
to the model's three camera inputs only to validate the interface. The raw six-dimensional output
has no Ackermann control semantics and must never be published directly to `/cmd_vel`.

For repeated Shadow inference, build and start the persistent runtime:

```bash
cp config/smolvla-runtime.env.example run/config/smolvla-runtime.env
./scripts/build_smolvla_runtime.sh
./scripts/run_smolvla_runtime.sh
./scripts/test_smolvla_runtime.sh
```

The persistent Provider keeps the model loaded and accepts real Observation requests over the Unix
Socket. Its default action adapter emits zero Twist candidates even though real model inference is
executed. See `docs/POLICY_RUNTIME.md` before enabling any explicit Shadow-only affine mapping.

## Vehicle Ops Console

Build and start the independent internal operations console:

```bash
./scripts/build.sh
./scripts/run_vehicle_ops_console.sh
```

The first run creates a random operator token under `run/config/vehicle_ops.env` and prints the LAN
URL. The offline responsive UI aggregates Supervisor, Observation, camera, Policy, Episode, Shadow,
Safety, and Orin host status. Token-protected write operations are restricted to task publication,
Episode Start/Stop, and Supervisor Safe Stop. It never publishes `/cmd_vel`, accesses the chassis
serial port, or executes arbitrary shell commands. The VLA Pipeline Inspector can freeze one Observation, run preprocessing, and execute exactly one Shadow-only inference while displaying the complete raw Action Chunk. See `docs/VEHICLE_OPS_CONSOLE.md`, `docs/VLA_PIPELINE_INSPECTOR.md`, and `docs/VLA_PIPELINE_TRACE.md`.

## Dataset export and replay

Convert a recorded Episode into the model-independent vehicle dataset and safely replay only its
Observations:

```bash
./scripts/export_episode.sh datasets/episodes/container-shadow-001
./scripts/replay_episode.sh datasets/episodes/container-shadow-001 1.0
./scripts/convert_lerobot_dataset.sh \
  datasets/lerobot/ackermann-shadow-v1 \
  datasets/exports/container-shadow-001
```

The exporter uses `observation_id` to join images, state, PolicyAction, and ShadowComparison instead
of guessing by timestamp. Replay publishes only `/vla/replay/observation` with refreshed IDs and
validity. The LeRobot converter uses the offline compatibility image, validates source FPS, maps
front RGB plus vehicle state into LeRobot features, and emits the Ackermann action target
`[linear_x, angular_z]`. See `docs/DATASET_EXPORT_REPLAY.md`.

Inspect data before training and create deterministic Episode-level splits before converting each
split separately:

```bash
./scripts/inspect_dataset.sh \
  datasets/exports/episode-001 \
  run/test/dataset-quality/episode-001
./scripts/split_vehicle_dataset.sh \
  datasets/splits/ackermann-v1.json \
  datasets/exports/episode-001 \
  datasets/exports/episode-002 \
  datasets/exports/episode-003
./scripts/convert_lerobot_split.sh \
  datasets/splits/ackermann-v1.json \
  train \
  datasets/lerobot/ackermann-train-v1
```

The split manifest preserves whole Episodes, source hashes, relative paths, and task/frame
statistics. The current all-zero-action Smoke Dataset intentionally reports
`training_readiness=false`; it validates interfaces but is not training data. See
`docs/DATASET_QUALITY.md`.

## SmolVLA training profiles

Run the five-step Orin CUDA/backward compatibility test:

```bash
./scripts/run_smolvla_training.sh \
  training/profiles/orin-smoke.json \
  datasets/lerobot/smolvla-training-smoke \
  run/training/orin-smoke-001
```

The same runner accepts `training/profiles/gpu-finetune.json` on an x86 CUDA workstation. It derives
policy features from the converted Dataset, resolves the pinned local VLM snapshot, remains offline,
and writes a reproducible training manifest. The Orin profile is a Smoke Test only and does not save
a deployable checkpoint. See `training/README.md`.

Do not source either legacy workspace before building or running this project. See
`DEPENDENCIES.md` for source provenance and licensing constraints.

- 组件编排与网页启停：`docs/COMPONENT_ORCHESTRATION.md`
