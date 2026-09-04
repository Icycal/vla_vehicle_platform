#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path


def schema_hash(action: dict) -> str:
    payload = json.dumps(
        {"schema": action["schema"], "features": action["features"]},
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return "sha256:" + hashlib.sha256(payload).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Attach a Chitu action descriptor to a model")
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--dataset", required=True, type=Path)
    arguments = parser.parse_args()
    model_dir = arguments.model_dir.resolve()
    dataset_manifest = json.loads(
        (arguments.dataset.resolve() / "vehicle_conversion_manifest.json").read_text(
            encoding="utf-8"
        )
    )
    action = dataset_manifest.get("action_descriptor")
    if not isinstance(action, dict):
        raise ValueError("Dataset conversion manifest has no action descriptor")
    action = dict(action)
    action["schema_hash"] = schema_hash(action)
    manifest_path = model_dir / "vehicle_model_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
    manifest["schema_version"] = "chitu.policy-model.v2"
    manifest.setdefault(
        "inference",
        {
            "backend": "pytorch",
            "artifact_format": "safetensors",
            "weight_precision": "mixed",
            "activation_precision": "bfloat16",
            "quantization": {"engine": "none", "scheme": "none"},
        },
    )
    manifest["action"] = action
    manifest["dataset_id"] = dataset_manifest.get("repo_id", manifest.get("dataset_id", ""))
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(manifest_path)
    print("MODEL_ACTION_MANIFEST_WRITTEN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
