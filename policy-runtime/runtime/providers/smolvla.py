import json
import os
import threading
import time
from pathlib import Path

from runtime.action_adapters import create_action_adapter
from runtime.inference_backends import create_smolvla_backend
from runtime.model_manifest import load_model_runtime_manifest

from .base import PolicyProvider


DEFAULT_STATE_KEYS = (
    "observation.state.linear_velocity",
    "observation.state.angular_velocity",
    "observation.state.acceleration_x",
    "observation.state.acceleration_y",
    "observation.state.yaw_rate",
    "observation.state.battery_voltage",
)
DEFAULT_MODEL_CAMERA_KEYS = (
    "observation.images.camera1",
    "observation.images.camera2",
    "observation.images.camera3",
)


class SmolVLAPolicyProvider(PolicyProvider):
    def __init__(self) -> None:
        self._model_dir = Path(os.environ.get("SMOLVLA_MODEL_DIR", "/models/smolvla_base"))
        self._vlm_manifest = Path(
            os.environ.get("SMOLVLA_VLM_MANIFEST", "/models/huggingface/smolvlm-manifest.json")
        )
        self._source_image_key = os.environ.get(
            "SMOLVLA_SOURCE_IMAGE_KEY", "observation.images.front"
        )
        self._model_camera_keys = self._parse_csv(
            os.environ.get("SMOLVLA_MODEL_CAMERA_KEYS", ",".join(DEFAULT_MODEL_CAMERA_KEYS))
        )
        self._state_keys = self._parse_csv(
            os.environ.get("SMOLVLA_STATE_KEYS", ",".join(DEFAULT_STATE_KEYS))
        )
        self._require_valid_state = os.environ.get("SMOLVLA_REQUIRE_VALID_STATE", "0") == "1"
        self._allow_image_fallback = os.environ.get("SMOLVLA_ALLOW_IMAGE_FALLBACK", "1") == "1"
        self._control_period_ns = max(
            1_000_000, int(os.environ.get("SMOLVLA_CONTROL_PERIOD_MS", "100")) * 1_000_000
        )
        self._response_margin_ns = max(
            0, int(os.environ.get("SMOLVLA_RESPONSE_MARGIN_MS", "500")) * 1_000_000
        )
        self._adapter = create_action_adapter(self._model_dir)
        self._runtime_manifest = load_model_runtime_manifest(self._model_dir, "smolvla")
        self._predict_lock = threading.Lock()
        self._request_counter = 0
        self._ready = False
        self._status_message = "SmolVLA provider loading"
        self._last_inference_ms = 0.0
        self._model_id = os.environ.get("SMOLVLA_MODEL_ID", self._model_dir.name)
        self._load_error = None
        try:
            self._load_model()
            self._ready = True
            self._status_message = self._format_status("ready")
        except Exception as error:
            self._load_error = str(error)
            self._status_message = f"SmolVLA load failed: {error}"
            if os.environ.get("POLICY_FAIL_FAST", "0") == "1":
                raise

    @staticmethod
    def _parse_csv(value: str):
        values = tuple(item.strip() for item in value.split(",") if item.strip())
        if not values:
            raise ValueError("SmolVLA mapping cannot be empty")
        return values

    @property
    def provider_id(self) -> str:
        return "smolvla-runtime"

    @property
    def model_id(self) -> str:
        return self._model_id

    @property
    def action_schema(self) -> str:
        return self._adapter.action_schema

    @property
    def status_message(self) -> str:
        return self._status_message

    def ready(self) -> bool:
        return self._ready

    def _format_status(self, state: str) -> str:
        return (
            f"SmolVLA {state}; backend={self._runtime_manifest.inference.backend}; "
            f"precision={self._runtime_manifest.inference.weight_precision}; "
            f"adapter={self._adapter.adapter_id}; "
            f"last_inference_ms={self._last_inference_ms:.1f}"
        )

    def _load_model(self) -> None:
        if not self._model_dir.is_dir():
            raise FileNotFoundError(f"SmolVLA model directory not found: {self._model_dir}")
        self._backend = create_smolvla_backend(
            self._model_dir, self._vlm_manifest, self._runtime_manifest
        )
        self._backend.load()
        self._torch = self._backend.torch
        self._numpy = self._backend.numpy
        self._cv2 = self._backend.cv2
        self._device = self._backend.device
        self._load_seconds = self._backend.load_seconds
        if os.environ.get("SMOLVLA_WARMUP", "1") == "1":
            synthetic_image = self._numpy.zeros((256, 256, 3), dtype=self._numpy.uint8)
            synthetic_state = self._numpy.zeros(len(self._state_keys), dtype=self._numpy.float32)
            self._predict_raw(synthetic_image, synthetic_state, "system warmup")

    def _select_image(self, request):
        selected = next((image for image in request.images if image.key == self._source_image_key), None)
        if selected is None and self._allow_image_fallback and len(request.images) == 1:
            selected = request.images[0]
        if selected is None:
            raise ValueError(f"required image key is missing: {self._source_image_key}")
        encoded = self._numpy.frombuffer(selected.data, dtype=self._numpy.uint8)
        decoded_bgr = self._cv2.imdecode(encoded, self._cv2.IMREAD_COLOR)
        if decoded_bgr is None:
            raise ValueError(f"unable to decode image: {selected.key} format={selected.format}")
        decoded_rgb = self._cv2.cvtColor(decoded_bgr, self._cv2.COLOR_BGR2RGB)
        resized = self._cv2.resize(decoded_rgb, (256, 256), interpolation=self._cv2.INTER_AREA)
        metadata = {
            "source_key": selected.key,
            "format": selected.format,
            "encoded_bytes": len(selected.data),
            "original_shape": list(decoded_rgb.shape),
            "processed_shape": list(resized.shape),
            "model_camera_keys": list(self._model_camera_keys),
        }
        return self._numpy.ascontiguousarray(resized), metadata

    def _select_state(self, request):
        values = {item.key: item for item in request.state}
        state = []
        state_details = []
        for key in self._state_keys:
            item = values.get(key)
            if item is None:
                raise ValueError(f"required state key is missing: {key}")
            if self._require_valid_state and not item.valid:
                raise ValueError(f"required state key is invalid: {key}")
            value = float(item.value) if item.valid else 0.0
            state.append(value)
            state_details.append({"key": key, "value": value, "valid": bool(item.valid)})
        return self._numpy.asarray(state, dtype=self._numpy.float32), state_details

    def _prepare_model_input(self, image, state, task: str):
        raw_observation = {"observation.state": state}
        for camera_key in self._model_camera_keys:
            raw_observation[camera_key] = image
        return self._backend.prepare(raw_observation, task)

    def _infer_normalized_chunk(self, model_input):
        normalized_chunk = self._backend.predict(model_input)
        self._last_inference_ms = self._backend.last_inference_ms
        return normalized_chunk

    def _postprocess_actions(self, normalized_chunk, action_count: int):
        raw_actions = []
        for action_index in range(action_count):
            normalized_action = normalized_chunk[:, action_index]
            action = self._backend.postprocess(normalized_action)
            raw_actions.append(action.squeeze(0).detach().cpu().numpy().astype(float).tolist())
        return raw_actions

    def _predict_raw(self, image, state, task: str):
        model_input = self._prepare_model_input(image, state, task)
        normalized_chunk = self._infer_normalized_chunk(model_input)
        action_count = min(
            normalized_chunk.shape[1], int(os.environ.get("SMOLVLA_ACTION_HORIZON", "8"))
        )
        return self._postprocess_actions(normalized_chunk, action_count)

    def _tensor_summary(self, value):
        if self._torch.is_tensor(value):
            detached = value.detach()
            summary = {
                "shape": list(detached.shape),
                "dtype": str(detached.dtype).replace("torch.", ""),
                "device": str(detached.device),
                "elements": detached.numel(),
            }
            if detached.numel() and detached.is_floating_point():
                numeric = detached.float()
                summary.update(
                    minimum=float(numeric.min().item()),
                    maximum=float(numeric.max().item()),
                    mean=float(numeric.mean().item()),
                )
            return summary
        if isinstance(value, (list, tuple)):
            return {"type": type(value).__name__, "length": len(value)}
        return {"type": type(value).__name__}

    def _debug_result(self, request, stage: str):
        total_started = time.perf_counter()
        preprocessing_started = time.perf_counter()
        image, image_metadata = self._select_image(request)
        state, state_details = self._select_state(request)
        model_input = self._prepare_model_input(image, state, request.task)
        preprocessing_ms = (time.perf_counter() - preprocessing_started) * 1000.0
        encoded_ok, processed_jpeg = self._cv2.imencode(
            ".jpg", self._cv2.cvtColor(image, self._cv2.COLOR_RGB2BGR)
        )
        if not encoded_ok:
            raise RuntimeError("failed to encode processed debug image")
        result = {
            "schema_version": "vehicle.vla.debug.v1",
            "stage": stage,
            "provider_id": self.provider_id,
            "model_id": self.model_id,
            "runtime": self._backend.runtime_info,
            "observation": {
                "observation_id": request.observation_id,
                "task": request.task,
                "image": image_metadata,
                "state": state_details,
            },
            "preprocessing": {
                "resize": [256, 256],
                "color_space": "RGB",
                "model_inputs": {
                    key: self._tensor_summary(value) for key, value in model_input.items()
                },
            },
            "latency_ms": {"preprocessing": preprocessing_ms},
            "safety": {
                "operation_mode": os.environ.get("POLICY_OPERATION_MODE", "shadow"),
                "publishes_control": False,
            },
        }
        if stage == "inference":
            normalized_chunk = self._infer_normalized_chunk(model_input)
            postprocess_started = time.perf_counter()
            action_count = min(
                normalized_chunk.shape[1], int(os.environ.get("SMOLVLA_ACTION_HORIZON", "8"))
            )
            raw_actions = self._postprocess_actions(normalized_chunk, action_count)
            adapted_actions = self._adapter.adapt(raw_actions)
            result["raw_output"] = {
                "normalized_action_chunk": {
                    "shape": list(normalized_chunk.shape),
                    "dtype": str(normalized_chunk.dtype).replace("torch.", ""),
                    "values": normalized_chunk.squeeze(0).detach().cpu().float().numpy().tolist(),
                },
                "denormalized_actions": {
                    "shape": [len(raw_actions), len(raw_actions[0]) if raw_actions else 0],
                    "values": raw_actions,
                },
            }
            result["interpreted_output"] = {
                "adapter_id": self._adapter.adapter_id,
                "action_schema": self._adapter.action_schema,
                "schema_hash": self._adapter.schema_hash,
                "feature_names": list(self._adapter.feature_names),
                "feature_units": list(self._adapter.feature_units),
                "actions": [{"values": list(values)} for values in adapted_actions],
            }
            if self._adapter.supports_legacy_twist:
                result["interpreted_output"]["twist_actions"] = [
                    {"linear_x": float(values[0]), "angular_z": float(values[1])}
                    for values in adapted_actions
                ]
            result["latency_ms"].update(
                inference=self._last_inference_ms,
                postprocessing=(time.perf_counter() - postprocess_started) * 1000.0,
            )
        result["latency_ms"]["total"] = (time.perf_counter() - total_started) * 1000.0
        return result, processed_jpeg.tobytes()

    def predict(self, request, protocol):
        if not self._ready:
            raise RuntimeError(self._status_message)
        if request.schema_version != "vehicle.observation.v1":
            raise ValueError(f"unsupported observation schema: {request.schema_version}")
        if request.valid_until_ns <= time.time_ns():
            raise ValueError("observation expired before inference")
        if not request.task.strip():
            raise ValueError("task is empty")
        if not self._predict_lock.acquire(blocking=False):
            raise RuntimeError("SmolVLA provider is busy")
        try:
            image, _ = self._select_image(request)
            state, _ = self._select_state(request)
            raw_actions = self._predict_raw(image, state, request.task)
            adapted_actions = self._adapter.adapt(raw_actions)
            generated_at = time.time_ns()
            self._request_counter += 1
            response = protocol.PredictResponse(
                request_id=f"smolvla-runtime-{self._request_counter}",
                observation_id=request.observation_id,
                provider_id=self.provider_id,
                model_id=self.model_id,
                action_schema=self._adapter.action_schema,
                action_schema_hash=self._adapter.schema_hash,
                generated_at_ns=generated_at,
                valid_until_ns=(
                    generated_at + self._control_period_ns * len(adapted_actions) + self._response_margin_ns
                ),
                control_period_ns=self._control_period_ns,
            )
            response.action_features.extend(self._adapter.feature_names)
            response.action_units.extend(self._adapter.feature_units)
            for values in adapted_actions:
                response.action_vectors.add().values.extend(values)
                if self._adapter.supports_legacy_twist:
                    response.actions.add(linear_x=values[0], angular_z=values[1])
            self._status_message = self._format_status("ready")
            raw_preview = raw_actions[0] if raw_actions else []
            print(
                f"SmolVLA prediction observation={request.observation_id} "
                f"inference_ms={self._last_inference_ms:.1f} "
                f"adapter={self._adapter.adapter_id} raw_first={raw_preview}",
                flush=True,
            )
            return response
        finally:
            self._predict_lock.release()

    def debug(self, request, protocol):
        if not self._ready:
            raise RuntimeError(self._status_message)
        stage = request.stage.strip().lower()
        if stage not in ("preprocess", "inference"):
            raise ValueError(f"unsupported debug stage: {request.stage}")
        observation = request.observation
        if observation.schema_version != "vehicle.observation.v1":
            raise ValueError(f"unsupported observation schema: {observation.schema_version}")
        if not observation.task.strip():
            raise ValueError("task is empty")
        if not self._predict_lock.acquire(blocking=False):
            raise RuntimeError("SmolVLA provider is busy")
        try:
            result, processed_image = self._debug_result(observation, stage)
            result["debug_run_id"] = request.debug_run_id
            return protocol.DebugResponse(
                debug_run_id=request.debug_run_id,
                provider_id=self.provider_id,
                model_id=self.model_id,
                schema_version="vehicle.vla.debug.v1",
                result_json=json.dumps(result, ensure_ascii=False, separators=(",", ":")),
                processed_image_jpeg=processed_image,
            )
        finally:
            self._predict_lock.release()
