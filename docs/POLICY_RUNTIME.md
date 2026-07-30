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

## Phase 1 Acceptance

- `/vla/policy_state` reports provider `mock-runtime` and model
  `mock-runtime-zero-policy-v1`.
- `PolicyAction.observation_id` matches the source Observation.
- Shadow comparison and metrics are generated with zero Mock MAE.
- Final `/cmd_vel` remains zero.
- `wheeltec_robot_node` is not started.
- A correlated Episode records Observation, Policy Action, Shadow Comparison, Shadow Metrics, and
  compressed images.
