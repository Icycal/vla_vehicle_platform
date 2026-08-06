# Dataset Export and Observation Replay

## Purpose

Phase 1 records a model-independent `vehicle.episode.v1` rosbag2 Episode. Training frameworks
must not read project bags directly because each VLA implementation has different feature names,
normalization, and dataset packaging. The first export layer therefore produces a stable vehicle
intermediate dataset. Model-specific exporters, including a future LeRobot exporter, consume this
intermediate format.

Observation Replay republishes only `vehicle_interfaces/msg/PolicyObservation`. It never replays
`/cmd_vel`, selected commands, safety events, or chassis traffic.

## Vehicle Dataset v1

Export an Episode:

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/build.sh
./scripts/export_episode.sh \
  datasets/episodes/container-shadow-001
```

The default output is:

```text
datasets/exports/<episode-id>/
|-- dataset_manifest.json
|-- frames.jsonl
`-- images/
```

Each `vehicle.dataset.frame.v1` JSONL record contains:

- Episode and Observation correlation identifiers;
- task text and source timestamps;
- named state values with validity flags;
- original compressed Observation images and relative paths;
- the correlated PolicyAction chunk when available;
- the correlated ShadowComparison executed Twist as the training target;
- predicted Twist and Shadow error values for analysis.

By default, Observations without a correlated ShadowComparison are counted but skipped. Add
`--include-incomplete` after the output argument when diagnostics require them:

```bash
./scripts/export_episode.sh \
  datasets/episodes/container-shadow-001 \
  datasets/exports/container-shadow-001-all \
  --include-incomplete
```

The exporter refuses to overwrite an existing output directory. It writes to a sibling `.partial`
directory and renames it only after all frames, images, and the Manifest are complete.

## Safe Observation Replay

Replay the original timing:

```bash
./scripts/replay_episode.sh \
  datasets/episodes/container-shadow-001 \
  1.0
```

Arguments are Episode path, playback rate, and optional maximum Observation count. Replay publishes
to `/vla/replay/observation`, creates a new `replay-...` Observation ID, and refreshes Observation
and image timestamps plus the validity window.

The node rejects any output outside `/vla/replay/` unless its explicit `allow_live_topic` parameter
is set. The project scripts never enable that override.

## SmolVLA Replay Shadow

Start the persistent runtime:

```bash
./scripts/run_smolvla_runtime.sh
```

Start only the Phase 0 control-safety chain with the replay-specific Gateway configuration:

```bash
./scripts/run_phase0.sh \
  policy_params_file:=/home/wheeltec/vla_vehicle_platform/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/policy_smolvla_replay.yaml
```

Then replay an Episode in another isolated project terminal:

```bash
./scripts/replay_episode.sh datasets/episodes/container-shadow-001 1.0
```

This path does not start the camera or `wheeltec_robot_node`. The current SmolVLA Shadow adapter
returns zero Twist candidates, and `vla_safety_guard` remains the only final `/cmd_vel` publisher.
Stop the runtime after the test:

```bash
./scripts/stop_smolvla_runtime.sh
```

## Current Boundary

`vehicle.dataset.v1` is not yet a native LeRobot dataset. A subsequent LeRobot Dataset Exporter
will map named vehicle states, images, tasks, and Ackermann targets into the exact feature schema of
a vehicle-trained policy. The base SmolVLA checkpoint remains unsuitable for vehicle control.
