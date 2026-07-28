# Source dependency inventory

Snapshot date: 2026-07-28

The greenfield workspace keeps vehicle-specific and navigation source dependencies under
`third_party/` and links them into `ros_ws/src/vendor` with `scripts/prepare_workspace.sh`.
The new platform must not source `/home/wheeltec/wheeltec_ros2/install` or
`/home/wheeltec/workspace_rhzd/install`.

| Component | Vendored path | Recorded revision | State at import |
| --- | --- | --- | --- |
| Nav2 Humble | `third_party/wheeltec_ros2/navigation2-humble` | From Wheeltec monorepo `21def27f9a00cc2ec58914572ac34a093c495578` | Source snapshot, Git metadata not copied |
| Chassis driver | `third_party/wheeltec_ros2/turn_on_wheeltec_robot` | Wheeltec monorepo `21def27f9a00cc2ec58914572ac34a093c495578` | Behavior-compatible snapshot; one upstream whitespace-only difference was observed before import |
| Wheeltec messages | `third_party/wheeltec_ros2/wheeltec_robot_msg` | Wheeltec monorepo `21def27f9a00cc2ec58914572ac34a093c495578` | Source snapshot, Git metadata not copied |
| Serial library | `third_party/wheeltec_ros2/depend/serial_ros2` | `def526d378501e0d19d0a8724971688a3b174080` | Imported from a locally modified working tree; Git metadata not copied |

## Local compatibility changes

- Removed the unused `rclpy` build/runtime dependency from the imported chassis package.
- Removed the unused `ackermann_msgs` manifest dependency from the imported chassis package.
- The chassis implementation remains C++ and continues to publish/subscribe using its existing ROS interfaces.

## License and redistribution warning

`turn_on_wheeltec_robot` and `wheeltec_robot_msg` declare `TODO: License declaration` and no
separate license file was found in the imported source. Keep these packages internal until the
license is clarified with the supplier. Do not externally redistribute this snapshot as an
open-source bundle without completing that review.

## Dependency boundary

Repository source is vendored where it is vehicle-specific or known to have been supplied by an
old source workspace. Standard ROS 2 Humble, CUDA, JetPack, compiler, and Ubuntu libraries remain
system dependencies. They should be installed in the host or build image at pinned versions rather
than copied from an old `install/` directory.

## Jetson validation

Validated on 2026-07-28 at `/home/wheeltec/vla_vehicle_platform` using only the ROS Humble
underlay and the new workspace overlay:

- `vehicle_interfaces`, `vehicle_runtime`, and `vehicle_bringup` build successfully.
- `serial`, `wheeltec_robot_msg`, `nav2_common`, `nav2_msgs`, and
  `turn_on_wheeltec_robot` build successfully from the vendored source snapshot.
- Phase 0 runs in `ROS_DOMAIN_ID=42`, publishes a zero final `/cmd_vel`, accepts Shadow mode,
  and enters SafeStop on request without launching the chassis driver.
- The imported serial and chassis sources emit existing compiler warnings. These are recorded
  technical debt and must be addressed before a later production hardening release; they were not
  changed during the behavior-compatible import.
