from abc import ABC, abstractmethod


class ActionAdapter(ABC):
    @property
    @abstractmethod
    def adapter_id(self) -> str:
        raise NotImplementedError

    @property
    @abstractmethod
    def action_schema(self) -> str:
        raise NotImplementedError

    @property
    @abstractmethod
    def schema_hash(self) -> str:
        raise NotImplementedError

    @property
    @abstractmethod
    def feature_names(self):
        raise NotImplementedError

    @property
    @abstractmethod
    def feature_units(self):
        raise NotImplementedError

    @property
    def supports_legacy_twist(self) -> bool:
        return False

    @abstractmethod
    def adapt(self, raw_actions):
        raise NotImplementedError
