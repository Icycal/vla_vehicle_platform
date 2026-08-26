import json
import math
from pathlib import Path

from .base import ActionAdapter
from .descriptor import ActionDescriptor


class ManifestActionAdapter(ActionAdapter):
    def __init__(self, model_dir: Path, horizon: int) -> None:
        manifest_path = model_dir / "vehicle_model_manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        action = manifest.get("action")
        if not isinstance(action, dict):
            raise ValueError(f"model manifest has no action descriptor: {manifest_path}")
        features = action.get("features")
        if not isinstance(features, list) or not features:
            raise ValueError("model action descriptor must contain features")
        ordered = sorted(features, key=lambda item: int(item["index"]))
        expected_indices = list(range(len(ordered)))
        actual_indices = [int(item["index"]) for item in ordered]
        if actual_indices != expected_indices:
            raise ValueError("model action feature indices must be contiguous from zero")
        self._descriptor = ActionDescriptor(
            schema=str(action["schema"]),
            feature_names=tuple(str(item["name"]) for item in ordered),
            feature_units=tuple(str(item.get("unit", "")) for item in ordered),
        )
        declared_hash = str(action.get("schema_hash", ""))
        if declared_hash and declared_hash != self._descriptor.schema_hash:
            raise ValueError("model action schema hash does not match its descriptor")
        self._horizon = max(1, horizon)

    @property
    def adapter_id(self) -> str:
        return "smolvla-manifest-codec-v1"

    @property
    def action_schema(self) -> str:
        return self._descriptor.schema

    @property
    def schema_hash(self) -> str:
        return self._descriptor.schema_hash

    @property
    def feature_names(self):
        return self._descriptor.feature_names

    @property
    def feature_units(self):
        return self._descriptor.feature_units

    @property
    def supports_legacy_twist(self) -> bool:
        return self.feature_names == ("linear_x", "angular_z")

    def adapt(self, raw_actions):
        action_count = min(len(raw_actions), self._horizon)
        if action_count == 0:
            raise ValueError("SmolVLA produced an empty action chunk")
        expected_size = len(self.feature_names)
        adapted = []
        for action in raw_actions[:action_count]:
            values = [float(value) for value in action]
            if len(values) != expected_size:
                raise ValueError(
                    f"model action dimension {len(values)} does not match manifest {expected_size}"
                )
            if not all(math.isfinite(value) for value in values):
                raise ValueError("SmolVLA action contains a non-finite value")
            adapted.append(values)
        return adapted
