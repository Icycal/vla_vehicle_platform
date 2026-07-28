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
- `ros_ws/src/vehicle_bringup`: XML launch files and YAML parameters.
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
./scripts/start_episode.sh episode-test "front camera validation" wheeltec
./scripts/stop_episode.sh true "validation complete"
```

Episodes are stored under `datasets/episodes` as rosbag2 data plus an
`episode_manifest.json`. The current camera calibration file is a placeholder; complete an actual
checkerboard calibration before enabling `require_calibration` or using images for geometric tasks.
See `docs/CAMERA_CALIBRATION.md` for the acceptance checklist.

Do not source either legacy workspace before building or running this project. See
`DEPENDENCIES.md` for source provenance and licensing constraints.
