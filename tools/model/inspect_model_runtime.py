#!/usr/bin/env python3

import argparse
import importlib.util
import json
import platform
from pathlib import Path


def normalized_architecture(value: str) -> str:
    aliases = {"amd64": "x86_64", "arm64": "aarch64"}
    normalized = value.strip().lower()
    return aliases.get(normalized, normalized)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a Chitu model runtime descriptor")
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--runtime-image-check", action="store_true")
    arguments = parser.parse_args()
    model_dir = arguments.model_dir.resolve()
    manifest_path = model_dir / "vehicle_model_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
    inference = manifest.get("inference") or manifest.get("runtime") or {}
    backend = str(inference.get("backend", "pytorch")).lower()
    precision = str(inference.get("weight_precision", inference.get("precision", "mixed"))).lower()
    activation = str(inference.get("activation_precision", "bfloat16")).lower()
    if backend != "pytorch":
        raise ValueError(f"Unsupported inference backend: {backend}")
    if precision not in ("mixed", "float32", "float16", "bfloat16", "int8"):
        raise ValueError(f"Unsupported weight precision: {precision}")
    compatibility = manifest.get("compatibility") or {}
    platforms = [str(value).lower() for value in compatibility.get("platforms", [])]
    architectures = [
        normalized_architecture(str(value)) for value in compatibility.get("architectures", [])
    ]
    current_platform = platform.system().lower()
    current_architecture = normalized_architecture(platform.machine())
    if platforms and current_platform not in platforms:
        raise RuntimeError(f"Model supports platforms {platforms}, current platform is {current_platform}")
    if architectures and current_architecture not in architectures:
        raise RuntimeError(
            f"Model supports architectures {architectures}, current architecture is {current_architecture}"
        )
    if arguments.runtime_image_check and precision == "int8":
        quantization = inference.get("quantization") or {}
        if quantization.get("engine", "pytorch-native") == "torchao" and importlib.util.find_spec("torchao") is None:
            raise RuntimeError("INT8 model requires torchao in the policy runtime image")
    print(json.dumps({
        "backend": backend,
        "weight_precision": precision,
        "activation_precision": activation,
        "platform": current_platform,
        "architecture": current_architecture,
        "compatible": True,
    }, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
