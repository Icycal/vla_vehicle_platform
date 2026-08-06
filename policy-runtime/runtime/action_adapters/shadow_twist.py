import math
import os

from .base import ActionAdapter


class ShadowTwistActionAdapter(ActionAdapter):
    def __init__(
        self,
        mode: str,
        horizon: int,
        linear_index: int,
        angular_index: int,
        linear_scale: float,
        angular_scale: float,
        linear_offset: float,
        angular_offset: float,
        linear_limit: float,
        angular_limit: float,
    ) -> None:
        normalized_mode = mode.strip().lower()
        if normalized_mode not in ("zero", "affine"):
            raise ValueError(f"unsupported SmolVLA action adapter mode: {mode}")
        operation_mode = os.environ.get("POLICY_OPERATION_MODE", "shadow").strip().lower()
        if operation_mode != "shadow":
            raise ValueError("SmolVLA action adapters are restricted to Shadow mode")
        if normalized_mode == "affine" and os.environ.get(
            "SMOLVLA_ALLOW_UNTRAINED_ADAPTER", "0"
        ) != "1":
            raise ValueError(
                "the untrained affine adapter requires SMOLVLA_ALLOW_UNTRAINED_ADAPTER=1"
            )
        if linear_index < 0 or angular_index < 0:
            raise ValueError("SmolVLA action indices must be non-negative")
        self._mode = normalized_mode
        self._horizon = max(1, horizon)
        self._linear_index = linear_index
        self._angular_index = angular_index
        self._linear_scale = linear_scale
        self._angular_scale = angular_scale
        self._linear_offset = linear_offset
        self._angular_offset = angular_offset
        self._linear_limit = max(0.0, linear_limit)
        self._angular_limit = max(0.0, angular_limit)

    @classmethod
    def from_environment(cls):
        return cls(
            mode=os.environ.get("SMOLVLA_ACTION_ADAPTER", "zero"),
            horizon=int(os.environ.get("SMOLVLA_ACTION_HORIZON", "8")),
            linear_index=int(os.environ.get("SMOLVLA_LINEAR_ACTION_INDEX", "0")),
            angular_index=int(os.environ.get("SMOLVLA_ANGULAR_ACTION_INDEX", "1")),
            linear_scale=float(os.environ.get("SMOLVLA_LINEAR_ACTION_SCALE", "0.0")),
            angular_scale=float(os.environ.get("SMOLVLA_ANGULAR_ACTION_SCALE", "0.0")),
            linear_offset=float(os.environ.get("SMOLVLA_LINEAR_ACTION_OFFSET", "0.0")),
            angular_offset=float(os.environ.get("SMOLVLA_ANGULAR_ACTION_OFFSET", "0.0")),
            linear_limit=float(os.environ.get("SMOLVLA_LINEAR_LIMIT", "0.2")),
            angular_limit=float(os.environ.get("SMOLVLA_ANGULAR_LIMIT", "0.5")),
        )

    @property
    def adapter_id(self) -> str:
        if self._mode == "zero":
            return "smolvla-shadow-zero-v1"
        return "smolvla-untrained-affine-shadow-v1"

    def adapt(self, raw_actions):
        action_count = min(len(raw_actions), self._horizon)
        if action_count == 0:
            raise ValueError("SmolVLA produced an empty action chunk")
        if self._mode == "zero":
            return [(0.0, 0.0) for _ in range(action_count)]

        adapted_actions = []
        for action in raw_actions[:action_count]:
            action_values = list(action)
            required_index = max(self._linear_index, self._angular_index)
            if required_index >= len(action_values):
                raise ValueError(
                    f"raw action dimension {len(action_values)} does not contain index {required_index}"
                )
            linear_value = (
                float(action_values[self._linear_index]) * self._linear_scale
                + self._linear_offset
            )
            angular_value = (
                float(action_values[self._angular_index]) * self._angular_scale
                + self._angular_offset
            )
            if not math.isfinite(linear_value) or not math.isfinite(angular_value):
                raise ValueError("SmolVLA action adapter produced a non-finite value")
            linear_value = max(-self._linear_limit, min(self._linear_limit, linear_value))
            angular_value = max(-self._angular_limit, min(self._angular_limit, angular_value))
            adapted_actions.append((linear_value, angular_value))
        return adapted_actions
