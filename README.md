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

Do not source either legacy workspace before building or running this project. See
`DEPENDENCIES.md` for source provenance and licensing constraints.
