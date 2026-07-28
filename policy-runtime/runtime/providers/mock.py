import os
import time

from .base import PolicyProvider


class MockPolicyProvider(PolicyProvider):
    def __init__(self) -> None:
        self._horizon = max(1, int(os.environ.get("MOCK_ACTION_HORIZON", "8")))
        self._control_period_ns = max(
            1_000_000, int(os.environ.get("MOCK_CONTROL_PERIOD_MS", "50")) * 1_000_000
        )
        self._linear_velocity = float(os.environ.get("MOCK_LINEAR_VELOCITY", "0.0"))
        self._angular_velocity = float(os.environ.get("MOCK_ANGULAR_VELOCITY", "0.0"))
        self._request_counter = 0

    @property
    def provider_id(self) -> str:
        return "mock-runtime"

    @property
    def model_id(self) -> str:
        return "mock-runtime-zero-policy-v1"

    def ready(self) -> bool:
        return True

    def predict(self, request, protocol):
        if request.schema_version != "vehicle.observation.v1":
            raise ValueError(f"unsupported observation schema: {request.schema_version}")
        if not request.images:
            raise ValueError("at least one image is required")
        if request.valid_until_ns <= time.time_ns():
            raise ValueError("observation expired")

        self._request_counter += 1
        generated_at = time.time_ns()
        response = protocol.PredictResponse(
            request_id=f"mock-runtime-{self._request_counter}",
            observation_id=request.observation_id,
            provider_id=self.provider_id,
            model_id=self.model_id,
            action_schema="vehicle.twist_chunk.v1",
            generated_at_ns=generated_at,
            valid_until_ns=generated_at + self._control_period_ns * self._horizon + 500_000_000,
            control_period_ns=self._control_period_ns,
        )
        linear_velocity = self._linear_velocity if request.task else 0.0
        angular_velocity = self._angular_velocity if request.task else 0.0
        for _ in range(self._horizon):
            response.actions.add(linear_x=linear_velocity, angular_z=angular_velocity)
        return response
