import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Tuple


SUPPORTED_BACKENDS = ("pytorch",)
SUPPORTED_WEIGHT_PRECISIONS = ("mixed", "float32", "float16", "bfloat16", "int8")


@dataclass(frozen=True)
class QuantizationSpec:
    engine: str = "none"
    scheme: str = "none"
    weight_dtype: str = "bfloat16"
    activation_dtype: str = "bfloat16"
    include_modules: Tuple[str, ...] = ()
    exclude_modules: Tuple[str, ...] = ()


@dataclass(frozen=True)
class InferenceSpec:
    backend: str = "pytorch"
    artifact_format: str = "safetensors"
    weight_precision: str = "mixed"
    activation_precision: str = "bfloat16"
    quantization: QuantizationSpec = field(default_factory=QuantizationSpec)


@dataclass(frozen=True)
class ModelRuntimeManifest:
    schema_version: str
    provider: str
    inference: InferenceSpec
    raw: Dict[str, Any]
    legacy: bool = False


def _tuple(value: Any) -> Tuple[str, ...]:
    if value is None:
        return ()
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError("Quantization module filters must be string arrays")
    return tuple(item.strip() for item in value if item.strip())


def _legacy_manifest(provider: str) -> ModelRuntimeManifest:
    raw = {
        "schema_version": "chitu.policy-model.v2",
        "provider": provider,
        "inference": {
            "backend": "pytorch",
            "artifact_format": "safetensors",
            "weight_precision": "mixed",
            "activation_precision": "bfloat16",
            "quantization": {"engine": "none", "scheme": "none"},
        },
    }
    return ModelRuntimeManifest(
        schema_version=raw["schema_version"],
        provider=provider,
        inference=InferenceSpec(),
        raw=raw,
        legacy=True,
    )


def load_model_runtime_manifest(model_dir: Path, expected_provider: str) -> ModelRuntimeManifest:
    manifest_path = model_dir / "vehicle_model_manifest.json"
    if not manifest_path.is_file():
        return _legacy_manifest(expected_provider)

    raw = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("Model manifest must be a JSON object")
    provider = str(raw.get("provider") or expected_provider).strip().lower()
    if provider != expected_provider:
        raise ValueError(
            f"Model provider {provider!r} does not match expected provider {expected_provider!r}"
        )
    inference_value = raw.get("inference") or raw.get("runtime") or {}
    if not isinstance(inference_value, dict):
        raise ValueError("Model inference descriptor must be an object")
    backend = str(inference_value.get("backend", "pytorch")).strip().lower()
    artifact_format = str(
        inference_value.get("artifact_format", inference_value.get("model_format", "safetensors"))
    ).strip().lower()
    weight_precision = str(
        inference_value.get("weight_precision", inference_value.get("precision", "mixed"))
    ).strip().lower()
    activation_precision = str(
        inference_value.get("activation_precision", "bfloat16")
    ).strip().lower()
    if backend not in SUPPORTED_BACKENDS:
        raise ValueError(f"Unsupported inference backend: {backend}")
    if weight_precision not in SUPPORTED_WEIGHT_PRECISIONS:
        raise ValueError(f"Unsupported model weight precision: {weight_precision}")

    quantization_value = inference_value.get("quantization") or {}
    if not isinstance(quantization_value, dict):
        raise ValueError("Model quantization descriptor must be an object")
    default_engine = "pytorch-native" if weight_precision == "int8" else "none"
    default_scheme = "weight_only" if weight_precision == "int8" else "none"
    quantization = QuantizationSpec(
        engine=str(quantization_value.get("engine", default_engine)).strip().lower(),
        scheme=str(quantization_value.get("scheme", default_scheme)).strip().lower(),
        weight_dtype=str(quantization_value.get("weight_dtype", weight_precision)).strip().lower(),
        activation_dtype=str(
            quantization_value.get("activation_dtype", activation_precision)
        ).strip().lower(),
        include_modules=_tuple(quantization_value.get("include_modules")),
        exclude_modules=_tuple(quantization_value.get("exclude_modules")),
    )
    if weight_precision == "int8":
        if quantization.engine not in ("pytorch-native", "torchao") or quantization.scheme != "weight_only":
            raise ValueError(
                "INT8 models require pytorch-native or torchao weight_only quantization"
            )
        if quantization.activation_dtype not in ("bfloat16", "float16"):
            raise ValueError("INT8 weight-only activation dtype must be bfloat16 or float16")

    return ModelRuntimeManifest(
        schema_version=str(raw.get("schema_version", "vehicle.policy-model.v1")),
        provider=provider,
        inference=InferenceSpec(
            backend=backend,
            artifact_format=artifact_format,
            weight_precision=weight_precision,
            activation_precision=activation_precision,
            quantization=quantization,
        ),
        raw=raw,
        legacy="inference" not in raw and "runtime" not in raw,
    )
