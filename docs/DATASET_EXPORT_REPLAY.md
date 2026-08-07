# Dataset Export, LeRobot Conversion, and Observation Replay

## Purpose

Phase 1 records a model-independent `vehicle.episode.v1` rosbag2 Episode. Training frameworks
must not read project bags directly because each VLA implementation has different feature names,
normalization, and dataset packaging. The first export layer therefore produces a stable vehicle
intermediate dataset. Model-specific converters consume this intermediate format.

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

## Native LeRobot Dataset

Convert one or more `vehicle.dataset.v1` directories into a local LeRobot v3 dataset:

```bash
./scripts/convert_lerobot_dataset.sh \
  datasets/lerobot/ackermann-shadow-v1 \
  datasets/exports/episode-001 \
  datasets/exports/episode-002
```

The wrapper runs fully offline in `vla-lerobot-compat:0.4.3`, does not require GPU access, and
verifies the completed dataset by loading its first and last samples through `LeRobotDataset`.
Output is written with the calling user's UID and GID. Existing output and `.partial` directories
are rejected.

The default mapping is `config/lerobot_ackermann_dataset.json`:

- `observation.images.front`: the RGB front camera image;
- `observation.state`: eight configured vehicle state values;
- `observation.state_valid`: a float validity mask for those state values;
- `action`: Ackermann training target `[linear_x, angular_z]` from the executed Shadow Twist;
- `source.timestamp_ns` and `source.frame_index`: source audit fields;
- task text: the original frame task stored through LeRobot task indexing.

Invalid state values are filled with the configured value, currently zero, while the validity mask
preserves whether each value was observed. The mapping is configuration-driven so another camera,
state vector, action dimension, repository ID, or robot type does not require converter code changes.
Use `LEROBOT_MAPPING_FILE` to select another tracked or runtime mapping file.

LeRobot datasets require one integer FPS. The converter does not silently resample frames. It checks
the median source interval against the configured FPS and rejects mismatches outside
`fps_tolerance_ratio`. The current recorder and default mapping are 5 FPS. Original nanosecond
timestamps remain available in `source.timestamp_ns` for audit and later explicit resampling.

The generated `vehicle_conversion_manifest.json` records the mapping, feature schema, source Episode
IDs, measured FPS, and frame counts. The current `container-shadow-001` sample contains all-zero
executed actions and is only a format/loader validation dataset, not useful vehicle training data.

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

The LeRobot converter establishes packaging and feature contracts; it does not make the base
SmolVLA checkpoint suitable for Ackermann control. Production training still requires calibrated
camera data, valid vehicle states, non-zero expert actions, train/validation splits, external GPU
fine-tuning, and a trained Ackermann Action Adapter. Until those gates pass, SmolVLA remains
Shadow-only and must not control `/cmd_vel`.