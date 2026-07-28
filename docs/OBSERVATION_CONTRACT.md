# Policy observation contract

## Purpose

`vehicle.observation.v1` is the model-independent boundary between ROS vehicle data and a Policy
Provider. It is intentionally not named after SmolVLA so another VLA or replay provider can consume
the same contract.

ROS topic:

```text
/vla/observation
vehicle_interfaces/msg/PolicyObservation
```

## Correlation and validity

Each observation contains:

- `observation_id`: monotonically generated correlation identifier.
- `schema_version`: currently `vehicle.observation.v1`.
- `generated_at` and `valid_until`: the provider must reject expired input.
- `task`: current natural-language task.
- `image_keys` and `images`: aligned named compressed images.
- `state_keys`, `state`, and `state_valid`: aligned named scalar state values.

`PolicyAction.observation_id` must copy the input identifier. Shadow comparison and Episode data use
that field to trace observation → prediction → evaluation.

## Current image key

```text
observation.images.front
```

The image is JPEG-compressed before entering the contract. The Provider owns model-specific image
decoding, resizing, normalization, and tensor layout.

## Current state keys

```text
observation.state.linear_velocity
observation.state.angular_velocity
observation.state.acceleration_x
observation.state.acceleration_y
observation.state.yaw_rate
observation.state.executed_linear_command
observation.state.executed_angular_command
observation.state.battery_voltage
```

Missing inputs are represented by a zero value with the aligned `state_valid` element set to false.
Providers must validate required keys against their model Manifest rather than silently assuming all
values are valid.

## Adapter boundary

`observation_adapter` is responsible for ROS topic freshness, stable key naming, correlation IDs,
and model-independent packing. It must not perform model-specific normalization or padding.

The future SmolVLA Provider is responsible for:

- mapping image keys to model feature names;
- decoding and resizing images;
- mapping or padding the state vector;
- tokenizing the task;
- validating model capabilities and required state keys;
- returning a versioned action Chunk.

## Phase 1 safety rule

Observations and predictions may be generated in `VLA_SHADOW`, but predicted actions cannot become
the selected control source. `shadow_evaluator` compares predictions with actual `/cmd_vel` and
publishes `/vla/shadow_comparison` plus cumulative `/vla/shadow_metrics`.
