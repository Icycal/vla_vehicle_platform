import math
import os
import socket
import struct
import time
import uuid
from pathlib import Path

from generated import policy_protocol_pb2 as protocol


SOCKET_PATH = os.environ.get("POLICY_SOCKET_PATH", "/run/vla-policy/policy.sock")
IMAGE_PATH = Path(os.environ.get("SMOLVLA_SMOKE_IMAGE", "/test/images/front.jpg"))
STATE_KEYS = (
    "observation.state.linear_velocity",
    "observation.state.angular_velocity",
    "observation.state.acceleration_x",
    "observation.state.acceleration_y",
    "observation.state.yaw_rate",
    "observation.state.battery_voltage",
)


def transact(envelope, timeout):
    payload = envelope.SerializeToString()
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout)
        connection.connect(SOCKET_PATH)
        connection.sendall(struct.pack("!I", len(payload)) + payload)
        response_size = struct.unpack("!I", receive_exact(connection, 4))[0]
        response = protocol.Envelope()
        response.ParseFromString(receive_exact(connection, response_size))
        return response


def receive_exact(connection, size):
    payload = bytearray()
    while len(payload) < size:
        chunk = connection.recv(size - len(payload))
        if not chunk:
            raise ConnectionError("connection closed before response completed")
        payload.extend(chunk)
    return bytes(payload)


def main():
    health = protocol.Envelope(protocol_version="1", message_id="smoke-health")
    health.health_request.SetInParent()
    health_response = transact(health, 5.0)
    if not health_response.health_response.ready:
        raise RuntimeError(health_response.health_response.message)

    generated_at = time.time_ns()
    request = protocol.Envelope(
        protocol_version="1",
        message_id=f"smoke-{uuid.uuid4()}",
    )
    prediction = request.predict_request
    prediction.observation_id = request.message_id
    prediction.schema_version = "vehicle.observation.v1"
    prediction.task = os.environ.get("SMOLVLA_SMOKE_TASK", "move forward safely")
    prediction.generated_at_ns = generated_at
    prediction.valid_until_ns = generated_at + 10_000_000_000
    image = prediction.images.add()
    image.key = "observation.images.front"
    image.format = "jpeg"
    image.data = IMAGE_PATH.read_bytes()
    for state_key in STATE_KEYS:
        state = prediction.state.add()
        state.key = state_key
        state.value = 0.0
        state.valid = True

    started = time.perf_counter()
    response = transact(request, float(os.environ.get("SMOLVLA_SMOKE_TIMEOUT", "30")))
    latency_seconds = time.perf_counter() - started
    if response.HasField("error_response"):
        raise RuntimeError(
            f"{response.error_response.code}: {response.error_response.message}"
        )
    result = response.predict_response
    if result.observation_id != prediction.observation_id:
        raise RuntimeError("observation correlation mismatch")
    if not result.actions:
        raise RuntimeError("SmolVLA response contains no actions")
    for action in result.actions:
        if not math.isfinite(action.linear_x) or not math.isfinite(action.angular_z):
            raise RuntimeError("SmolVLA response contains non-finite Twist values")
    if os.environ.get("SMOLVLA_SMOKE_EXPECT_ZERO", "1") == "1":
        if any(
            abs(action.linear_x) > 1e-9 or abs(action.angular_z) > 1e-9
            for action in result.actions
        ):
            raise RuntimeError("default Shadow adapter produced a non-zero Twist candidate")
    print(f"provider_id={result.provider_id}")
    print(f"model_id={result.model_id}")
    print(f"action_count={len(result.actions)}")
    print(f"first_linear_x={result.actions[0].linear_x}")
    print(f"first_angular_z={result.actions[0].angular_z}")
    print(f"round_trip_seconds={latency_seconds:.3f}")
    print("SMOLVLA_RUNTIME_SMOKE_PASS")


if __name__ == "__main__":
    main()
