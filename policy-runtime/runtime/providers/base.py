from abc import ABC, abstractmethod


class PolicyProvider(ABC):
    @property
    @abstractmethod
    def provider_id(self) -> str:
        raise NotImplementedError

    @property
    @abstractmethod
    def model_id(self) -> str:
        raise NotImplementedError

    @property
    def observation_schema(self) -> str:
        return "vehicle.observation.v1"

    @property
    def action_schema(self) -> str:
        return "vehicle.twist_chunk.v1"

    @property
    def status_message(self) -> str:
        return "Policy runtime ready" if self.ready() else "Policy runtime unavailable"

    @abstractmethod
    def ready(self) -> bool:
        raise NotImplementedError

    @abstractmethod
    def predict(self, request, protocol):
        raise NotImplementedError

    def debug(self, request, protocol):
        raise NotImplementedError(f"provider {self.provider_id} does not support debug requests")
