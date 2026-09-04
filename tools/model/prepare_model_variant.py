#!/usr/bin/env python3

import argparse
import json
import os
import platform
import shutil
from datetime import datetime, timezone
from pathlib import Path


def copy_or_link(source: str, target: str) -> str:
    if Path(source).name == "vehicle_model_manifest.json":
        return shutil.copy2(source, target)
    try:
        os.link(source, target)
        return target
    except OSError:
        return shutil.copy2(source, target)


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a deployable Chitu policy model variant")
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--target", required=True, type=Path)
    parser.add_argument("--provider", default="smolvla")
    parser.add_argument("--backend", default="pytorch")
    parser.add_argument(
        "--weight-precision",
        default="int8",
        choices=("mixed", "float32", "float16", "bfloat16", "int8"),
    )
    parser.add_argument("--activation-precision", default="bfloat16")
    parser.add_argument("--dataset-id", default="")
    parser.add_argument("--target-platform", action="append", default=[])
    parser.add_argument("--target-architecture", action="append", default=[])
    parser.add_argument("--target-accelerator", action="append", default=[])
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    target = arguments.target.resolve()
    if not source.is_dir():
        raise FileNotFoundError(source)
    if target.exists():
        raise FileExistsError(target)
    for required in (
        "config.json",
        "model.safetensors",
        "policy_preprocessor.json",
        "policy_postprocessor.json",
    ):
        if not (source / required).is_file():
            raise FileNotFoundError(source / required)

    temporary = target.with_name(target.name + ".partial")
    if temporary.exists():
        shutil.rmtree(temporary)
    try:
        shutil.copytree(source, temporary, copy_function=copy_or_link)
        manifest_path = temporary / "vehicle_model_manifest.json"
        manifest = (
            json.loads(manifest_path.read_text(encoding="utf-8"))
            if manifest_path.is_file()
            else {}
        )
        manifest.update(
            {
                "schema_version": "chitu.policy-model.v2",
                "provider": arguments.provider,
                "dataset_id": arguments.dataset_id or manifest.get("dataset_id", ""),
                "variant_created_at": datetime.now(timezone.utc).isoformat(),
                "source_version": source.name,
                "inference": {
                    "backend": arguments.backend,
                    "artifact_format": "safetensors",
                    "source_weight_precision": "mixed",
                    "weight_precision": arguments.weight_precision,
                    "activation_precision": arguments.activation_precision,
                    "quantization": {
                        "engine": "pytorch-native" if arguments.weight_precision == "int8" else "none",
                        "scheme": "weight_only" if arguments.weight_precision == "int8" else "none",
                        "weight_dtype": arguments.weight_precision,
                        "activation_dtype": arguments.activation_precision,
                        "include_modules": ["model.vlm_with_expert.vlm"],
                        "exclude_modules": [],
                    },
                },
                "compatibility": {
                    "platforms": arguments.target_platform,
                    "architectures": arguments.target_architecture,
                    "accelerators": arguments.target_accelerator,
                },
                "build": {
                    "host_system": platform.system().lower(),
                    "host_architecture": platform.machine().lower(),
                },
            }
        )
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        temporary.rename(target)
    except Exception:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise
    print(target)
    print("MODEL_VARIANT_PREPARED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
