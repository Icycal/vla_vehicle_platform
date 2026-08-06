from abc import ABC, abstractmethod


class ActionAdapter(ABC):
    @property
    @abstractmethod
    def adapter_id(self) -> str:
        raise NotImplementedError

    @abstractmethod
    def adapt(self, raw_actions):
        raise NotImplementedError
