import json
import os
import threading
import time
from pathlib import Path

from runtime.action_adapters import create_action_adapter

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
            os.environ.get(
                "SMOLVLA_VLM_MANIFEST",
                "/models/huggingface/smolvlm-manifest.json",
            )
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
            1_000_000,
            int(os.environ.get("SMOLVLA_CONTROL_PERIOD_MS", "100")) * 1_000_000,
        )
        self._response_margin_ns = max(
            0,
            int(os.environ.get("SMOLVLA_RESPONSE_MARGIN_MS", "500")) * 1_000_000,
        )
        self._adapter = create_action_adapter()
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
    def status_message(self) -> str:
        return self._status_message

    def ready(self) -> bool:
        return self._ready

    def _format_status(self, state: str) -> str:
        return (
            f"SmolVLA {state}; adapter={self._adapter.adapter_id}; "
            f"last_inference_ms={self._last_inference_ms:.1f}"
        )

    def _load_model(self) -> None:
        import torch
        from lerobot.configs.policies import PreTrainedConfig
        from lerobot.policies.factory import make_pre_post_processors
        from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy
        from lerobot.utils.control_utils import prepare_observation_for_inference

        if not torch.cuda.is_available():
            raise RuntimeError("CUDA is not available")
        if not self._model_dir.is_dir():
            raise FileNotFoundError(f"SmolVLA model directory not found: {self._model_dir}")
        dependency = json.loads(self._vlm_manifest.read_text(encoding="utf-8"))
        vlm_path = Path(dependency["snapshot_path"])
        if not vlm_path.is_dir():
            raise FileNotFoundError(f"SmolVLM snapshot not found: {vlm_path}")

        config = PreTrainedConfig.from_pretrained(
            self._model_dir,
            local_files_only=True,
        )
        config.device = "cuda"
        config.vlm_model_name = str(vlm_path)
        load_started = time.perf_counter()
        policy = SmolVLAPolicy.from_pretrained(
            self._model_dir,
            config=config,
            local_files_only=True,
            strict=True,
        )
        policy.eval()
        policy.reset()
        preprocessor, postprocessor = make_pre_post_processors(
            policy_cfg=config,
            pretrained_path=str(self._model_dir),
            preprocessor_overrides={
                "tokenizer_processor": {"tokenizer_name": str(vlm_path)},
                "device_processor": {"device": "cuda"},
            },
        )
        torch.cuda.synchronize()
        self._torch = torch
        self._numpy = __import__("numpy")
        self._cv2 = __import__("cv2")
        self._policy = policy
        self._preprocessor = preprocessor
        self._postprocessor = postprocessor
        self._prepare_observation_for_inference = prepare_observation_for_inference
        self._device = torch.device("cuda")
        self._load_seconds = time.perf_counter() - load_started
        if os.environ.get("SMOLVLA_WARMUP", "1") == "1":
            synthetic_image = self._numpy.zeros((256, 256, 3), dtype=self._numpy.uint8)
            synthetic_state = self._numpy.zeros(len(self._state_keys), dtype=self._numpy.float32)
            self._predict_raw(synthetic_image, synthetic_state, "system warmup")

    def _select_image(self, request):
        selected = None
        for image in request.images:
            if image.key == self._source_image_key:
                selected = image
                break
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
        return self._numpy.ascontiguousarray(resized)

    def _select_state(self, request):
        values = {item.key: item for item in request.state}
        state = []
        for key in self._state_keys:
            item = values.get(key)
            if item is None:
                raise ValueError(f"required state key is missing: {key}")
            if self._require_valid_state and not item.valid:
                raise ValueError(f"required state key is invalid: {key}")
            state.append(float(item.value) if item.valid else 0.0)
        return self._numpy.asarray(state, dtype=self._numpy.float32)

    def _postprocess_actions(self, normalized_chunk, action_count: int):
        raw_actions = []
        for action_index in range(action_count):
            normalized_action = normalized_chunk[:, action_index]
            action = self._postprocessor(normalized_action)
            raw_actions.append(
                action.squeeze(0).detach().cpu().numpy().astype(float).tolist()
            )
        return raw_actions

    def _predict_raw(self, image, state, task: str):
        raw_observation = {"observation.state": state}
        for camera_key in self._model_camera_keys:
            raw_observation[camera_key] = image
        model_input = self._prepare_observation_for_inference(
            raw_observation,
            device=self._device,
            task=task,
        )
        model_input = self._preprocessor(model_input)
        self._torch.cuda.synchronize()
        inference_started = time.perf_counter()
        with self._torch.inference_mode():
            normalized_chunk = self._policy.predict_action_chunk(model_input)
        self._torch.cuda.synchronize()
        self._last_inference_ms = (time.perf_counter() - inference_started) * 1000.0
        action_count = min(
            normalized_chunk.shape[1],
            int(os.environ.get("SMOLVLA_ACTION_HORIZON", "8")),
        )
        return self._postprocess_actions(normalized_chunk, action_count)

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
            image = self._select_image(request)
            state = self._select_state(request)
            raw_actions = self._predict_raw(image, state, request.task)
            adapted_actions = self._adapter.adapt(raw_actions)
            generated_at = time.time_ns()
            self._request_counter += 1
            response = protocol.PredictResponse(
                request_id=f"smolvla-runtime-{self._request_counter}",
                observation_id=request.observation_id,
                provider_id=self.provider_id,
                model_id=self.model_id,
                action_schema="vehicle.twist_chunk.v1",
                generated_at_ns=generated_at,
                valid_until_ns=(
                    generated_at
                    + self._control_period_ns * len(adapted_actions)
                    + self._response_margin_ns
                ),
                control_period_ns=self._control_period_ns,
            )
            for linear_value, angular_value in adapted_actions:
                response.actions.add(linear_x=linear_value, angular_z=angular_value)
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
