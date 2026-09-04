import json
import time
from pathlib import Path

from runtime.quantization import apply_runtime_quantization


class PyTorchSmolVLABackend:
    backend_id = "pytorch"

    def __init__(self, model_dir: Path, vlm_manifest: Path, runtime_manifest) -> None:
        self.model_dir = model_dir
        self.vlm_manifest = vlm_manifest
        self.runtime_manifest = runtime_manifest
        self.last_inference_ms = 0.0

    def load(self) -> None:
        import cv2
        import numpy
        import torch
        from lerobot.configs.policies import PreTrainedConfig
        from lerobot.policies.factory import make_pre_post_processors
        from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy
        from lerobot.utils.control_utils import prepare_observation_for_inference

        if not torch.cuda.is_available():
            raise RuntimeError("CUDA is not available")
        dependency = json.loads(self.vlm_manifest.read_text(encoding="utf-8"))
        vlm_path = Path(dependency["snapshot_path"])
        if not vlm_path.is_dir():
            raise FileNotFoundError(f"SmolVLM snapshot not found: {vlm_path}")

        config = PreTrainedConfig.from_pretrained(self.model_dir, local_files_only=True)
        config.device = "cuda"
        config.vlm_model_name = str(vlm_path)
        started = time.perf_counter()
        policy = SmolVLAPolicy.from_pretrained(
            self.model_dir, config=config, local_files_only=True, strict=True
        )
        policy.eval()
        policy.reset()
        quantization = apply_runtime_quantization(policy, self.runtime_manifest.inference)
        preprocessor, postprocessor = make_pre_post_processors(
            policy_cfg=config,
            pretrained_path=str(self.model_dir),
            preprocessor_overrides={
                "tokenizer_processor": {"tokenizer_name": str(vlm_path)},
                "device_processor": {"device": "cuda"},
            },
        )
        torch.cuda.synchronize()
        self.torch = torch
        self.numpy = numpy
        self.cv2 = cv2
        self.policy = policy
        self.preprocessor = preprocessor
        self.postprocessor = postprocessor
        self.prepare_observation_for_inference = prepare_observation_for_inference
        self.device = torch.device("cuda")
        self.load_seconds = time.perf_counter() - started
        self.runtime_info = {
            "backend": self.backend_id,
            "artifact_format": self.runtime_manifest.inference.artifact_format,
            "weight_precision": self.runtime_manifest.inference.weight_precision,
            "activation_precision": self.runtime_manifest.inference.activation_precision,
            "quantization": quantization,
            "legacy_manifest": self.runtime_manifest.legacy,
        }

    def prepare(self, raw_observation, task: str):
        model_input = self.prepare_observation_for_inference(
            raw_observation, device=self.device, task=task
        )
        return self.preprocessor(model_input)

    def predict(self, model_input):
        self.torch.cuda.synchronize()
        started = time.perf_counter()
        with self.torch.inference_mode():
            normalized_chunk = self.policy.predict_action_chunk(model_input)
        self.torch.cuda.synchronize()
        self.last_inference_ms = (time.perf_counter() - started) * 1000.0
        return normalized_chunk

    def postprocess(self, normalized_action):
        return self.postprocessor(normalized_action)
