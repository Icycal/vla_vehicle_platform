import os
import signal
import socket
import struct
import threading
import uuid
from pathlib import Path

from generated import policy_protocol_pb2 as protocol

from .router import create_provider


PROTOCOL_VERSION = "1"
MAX_FRAME_SIZE = 32 * 1024 * 1024
DEFAULT_SOCKET_PATH = "/run/vla-policy/policy.sock"


def _receive_exact(connection: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = connection.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("connection closed before frame completed")
        chunks.extend(chunk)
    return bytes(chunks)


def _receive_envelope(connection: socket.socket) -> protocol.Envelope:
    frame_size = struct.unpack("!I", _receive_exact(connection, 4))[0]
    if frame_size == 0 or frame_size > MAX_FRAME_SIZE:
        raise ValueError(f"invalid frame size: {frame_size}")
    envelope = protocol.Envelope()
    envelope.ParseFromString(_receive_exact(connection, frame_size))
    return envelope


def _send_envelope(connection: socket.socket, envelope: protocol.Envelope) -> None:
    payload = envelope.SerializeToString()
    if not payload or len(payload) > MAX_FRAME_SIZE:
        raise ValueError(f"invalid response frame size: {len(payload)}")
    connection.sendall(struct.pack("!I", len(payload)) + payload)


def _error_envelope(message_id: str, code: str, message: str) -> protocol.Envelope:
    envelope = protocol.Envelope(
        protocol_version=PROTOCOL_VERSION,
        message_id=message_id or str(uuid.uuid4()),
    )
    envelope.error_response.code = code
    envelope.error_response.message = message
    return envelope


def _handle_request(request: protocol.Envelope, provider) -> protocol.Envelope:
    if request.protocol_version != PROTOCOL_VERSION:
        return _error_envelope(
            request.message_id,
            "UNSUPPORTED_PROTOCOL",
            f"expected protocol {PROTOCOL_VERSION}, got {request.protocol_version}",
        )

    payload = request.WhichOneof("payload")
    response = protocol.Envelope(
        protocol_version=PROTOCOL_VERSION,
        message_id=request.message_id,
    )
    if payload == "health_request":
        response.health_response.ready = provider.ready()
        response.health_response.provider_id = provider.provider_id
        response.health_response.model_id = provider.model_id
        response.health_response.observation_schema = "vehicle.observation.v1"
        response.health_response.action_schema = "vehicle.twist_chunk.v1"
        response.health_response.message = "Policy runtime ready" if provider.ready() else "Policy runtime unavailable"
        return response
    if payload == "predict_request":
        response.predict_response.CopyFrom(provider.predict(request.predict_request, protocol))
        return response
    return _error_envelope(request.message_id, "INVALID_REQUEST", "unsupported or missing payload")


def serve() -> None:
    socket_path = Path(os.environ.get("POLICY_SOCKET_PATH", DEFAULT_SOCKET_PATH))
    socket_path.parent.mkdir(parents=True, exist_ok=True)
    if socket_path.exists() or socket_path.is_socket():
        socket_path.unlink()

    provider = create_provider()
    stop_event = threading.Event()
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

    def request_shutdown(_signum, _frame) -> None:
        stop_event.set()
        server.close()

    signal.signal(signal.SIGINT, request_shutdown)
    signal.signal(signal.SIGTERM, request_shutdown)

    try:
        server.bind(str(socket_path))
        os.chmod(socket_path, 0o660)
        server.listen(8)
        server.settimeout(1.0)
        print(
            f"Policy runtime listening on {socket_path} "
            f"provider={provider.provider_id} model={provider.model_id}",
            flush=True,
        )
        while not stop_event.is_set():
            try:
                connection, _ = server.accept()
            except socket.timeout:
                continue
            except OSError:
                if stop_event.is_set():
                    break
                raise
            with connection:
                try:
                    request = _receive_envelope(connection)
                    response = _handle_request(request, provider)
                except Exception as error:
                    response = _error_envelope("", "RUNTIME_ERROR", str(error))
                try:
                    _send_envelope(connection, response)
                except (ConnectionError, OSError):
                    continue
    finally:
        server.close()
        if socket_path.exists() or socket_path.is_socket():
            socket_path.unlink()


if __name__ == "__main__":
    serve()
