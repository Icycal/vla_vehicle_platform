# Policy Runtime

## Boundary

Policy Runtime is a non-ROS process or container. It receives model-independent observations and
returns action chunks. ROS subscriptions, control arbitration, safety filtering, action timing,
and final `/cmd_vel` publication remain in project-owned C++ nodes on the host.

`vla_safety_guard` remains the sole final `/cmd_vel` publisher. A Policy Provider cannot directly
control the chassis.

## Components

- `runtime.server`: Unix Domain Socket server, framing, health, prediction, and shutdown handling.
- `runtime.router`: selects a Provider through `POLICY_PROVIDER`.
- `runtime.providers.mock`: zero-motion protocol validation Provider.
- `runtime.providers.smolvla`: persistent offline SmolVLA Provider for Shadow inference.
- `runtime.action_adapters`: model-output to vehicle-candidate adapters with Shadow enforcement.
- `vehicle_policy_transport`: C++ transport library used by `vla_policy_gateway`.
- `policy_protocol.proto`: versioned language-neutral wire contract.

Future SmolVLA or other VLA implementations add a Provider behind the router instead of changing
the ROS control chain.

## Protocol

The initial transport is an `AF_UNIX` stream socket. Each message is encoded as:

1. Four-byte unsigned network-order payload length.
2. Serialized Protobuf `Envelope` payload.

The maximum frame is 32 MiB. Protocol version `1` supports Health and Predict requests. Predict
contains the observation correlation ID, schema, task, timestamps, compressed images, and typed
state values. The response returns provider/model identity, the same observation ID, action schema,
validity, control period, and a Twist action chunk.

## Transport Selection

`vla_policy_gateway` supports:

- `mock`: in-process C++ zero-motion Provider used by default.
- `unix_socket`: external runtime at the configured `socket_path`.

Parameters are defined in `vehicle_bringup/config/phase0.yaml`; the external override is
`vehicle_bringup/config/policy_socket.yaml`.

The socket transport connects per request during Phase 1. Health is retried by the Gateway status
timer. Connection, framing, protocol, correlation, empty-action, and timeout failures mark the
runtime unavailable and suppress invalid `PolicyAction` publication.

## Host Mock Runtime

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/run_mock_policy_runtime.sh
```

JetPack currently provides `protoc 3.12.4` while the user Python environment provides protobuf
4.x. The host-only launcher selects protobuf's compatibility implementation for this Mock test.
The production model container must use matching compiler/runtime versions and does not inherit
this compatibility setting.

## Mock Container

```bash
cd /home/wheeltec/vla_vehicle_platform
docker compose -f policy-runtime/compose.mock.yaml build
docker compose -f policy-runtime/compose.mock.yaml up -d
```

The compose file exposes the same socket through the project `run/policy` directory. This image is
only for container and protocol validation. The later NVIDIA SmolVLA image will add JetPack-aligned
CUDA, PyTorch, LeRobot, model assets, GPU access, and a dedicated SmolVLA Provider.

The Mock build uses host networking only while installing build dependencies, avoiding the Jetson
kernel's unavailable Docker bridge `raw` table. The running container has networking disabled and
communicates only through the mounted Unix Socket. It runs as the configurable host UID/GID
(default `1000:1000`) so the C++ Gateway can access the `0660` socket without root privileges.

The Compose healthcheck sends a real Protobuf Health request over the Unix Socket. A live Python
process with a missing, blocked, or invalid protocol endpoint is therefore reported as unhealthy.

## Phase 1 Acceptance

- `/vla/policy_state` reports provider `mock-runtime` and model
  `mock-runtime-zero-policy-v1`.
- `PolicyAction.observation_id` matches the source Observation.
- Shadow comparison and metrics are generated with zero Mock MAE.
- Final `/cmd_vel` remains zero.
- `wheeltec_robot_node` is not started.
- A correlated Episode records Observation, Policy Action, Shadow Comparison, Shadow Metrics, and
  compressed images.


## Persistent SmolVLA Runtime

The SmolVLA Runtime image extends the validated `vla-lerobot-compat:0.4.3` image without replacing
NVIDIA PyTorch. It loads SmolVLA and SmolVLM2 once during container startup, performs an optional
warmup inference, and then serves repeated Predict requests over the existing Unix Socket.

Prepare the ignored runtime configuration and build the thin runtime image:

```bash
cd /home/wheeltec/vla_vehicle_platform
cp config/smolvla-runtime.env.example run/config/smolvla-runtime.env
./scripts/build_smolvla_runtime.sh
```

Start and validate it without ROS:

```bash
./scripts/run_smolvla_runtime.sh
./scripts/test_smolvla_runtime.sh
```

The smoke client sends the captured front-camera JPEG, six named state values, and a task through
the real Protobuf endpoint. Stop the runtime with:

```bash
./scripts/stop_smolvla_runtime.sh
```

To connect Phase 1 Shadow to the persistent runtime:

```bash
./scripts/run_phase1_shadow.sh \
  policy_params_file:=/home/wheeltec/vla_vehicle_platform/ros_ws/install/vehicle_bringup/share/vehicle_bringup/config/policy_smolvla_shadow.yaml
```

The Gateway uses a multithreaded executor so camera Observations and health reporting continue while
a long GPU inference is active. Only one prediction is admitted by the Provider; concurrent or
queued requests are rejected instead of accumulating stale camera frames.

## SmolVLA Action Adapter Safety

The base SmolVLA checkpoint produces a 50-by-6 pretrained robot action chunk. Those dimensions do
not have Ackermann vehicle semantics. The default `smolvla-shadow-zero-v1` adapter therefore runs
the real model but emits zero Twist candidates.

An `affine` adapter exists only for offline or real-vehicle Shadow analysis. It requires all of:

```text
POLICY_OPERATION_MODE=shadow
SMOLVLA_ACTION_ADAPTER=affine
SMOLVLA_ALLOW_UNTRAINED_ADAPTER=1
```

Its action indices, scales, offsets, and clamps are explicit environment values. This adapter is
not a trained vehicle controller and must never be enabled in Assisted or Autonomous mode. The
runtime rejects every non-Shadow operation mode before loading the model.
