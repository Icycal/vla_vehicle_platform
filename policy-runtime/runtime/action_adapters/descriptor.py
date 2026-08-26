import hashlib
import json
from dataclasses import dataclass


@dataclass(frozen=True)
class ActionDescriptor:
    schema: str
    feature_names: tuple[str, ...]
    feature_units: tuple[str, ...]

    def __post_init__(self):
        if not self.schema:
            raise ValueError("action schema cannot be empty")
        if not self.feature_names:
            raise ValueError("action features cannot be empty")
        if len(self.feature_names) != len(self.feature_units):
            raise ValueError("action feature names and units must have equal length")
        if len(set(self.feature_names)) != len(self.feature_names):
            raise ValueError("action feature names must be unique")

    @property
    def schema_hash(self) -> str:
        payload = json.dumps(
            {
                "schema": self.schema,
                "features": [
                    {"index": index, "name": name, "unit": unit}
                    for index, (name, unit) in enumerate(
                        zip(self.feature_names, self.feature_units)
                    )
                ],
            },
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        return "sha256:" + hashlib.sha256(payload).hexdigest()
