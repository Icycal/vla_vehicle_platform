import os
import socket
import struct
import uuid

from generated import policy_protocol_pb2 as protocol


PROTOCOL_VERSION = "1"
MAX_FRAME_SIZE = 32 * 1024 * 1024
DEFAULT_SOCKET_PATH = "/run/vla-policy/policy.sock"


def _receive_exact(connection: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = connection.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("connection closed before health response completed")
        chunks.extend(chunk)
    return bytes(chunks)


def check_health() -> bool:
    request = protocol.Envelope(
        protocol_version=PROTOCOL_VERSION,
        message_id=f"healthcheck-{uuid.uuid4()}",
    )
    request.health_request.SetInParent()
    payload = request.SerializeToString()

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(float(os.environ.get("POLICY_HEALTH_TIMEOUT", "1.0")))
        connection.connect(os.environ.get("POLICY_SOCKET_PATH", DEFAULT_SOCKET_PATH))
        connection.sendall(struct.pack("!I", len(payload)) + payload)
        response_size = struct.unpack("!I", _receive_exact(connection, 4))[0]
        if response_size == 0 or response_size > MAX_FRAME_SIZE:
            return False
        response = protocol.Envelope()
        response.ParseFromString(_receive_exact(connection, response_size))

    return (
        response.protocol_version == PROTOCOL_VERSION
        and response.HasField("health_response")
        and response.health_response.ready
    )


if __name__ == "__main__":
    raise SystemExit(0 if check_health() else 1)
