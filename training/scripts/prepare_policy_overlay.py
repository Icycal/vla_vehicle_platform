#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


PROFILE_SCHEMA = "vehicle.training.profile.v1"
DATASET_SCHEMA = "vehicle.lerobot.conversion.v1"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Prepare a local SmolVLA training policy overlay.")
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--base-model", required=True, type=Path)
    parser.add_argument("--vlm-manifest", required=True, type=Path)
    parser.add_argument("--hf-home", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def require_boolean(mapping: dict, key: str) -> bool:
    value = mapping.get(key)
    if not isinstance(value, bool):
        raise ValueError(f"{key} must be boolean")
    return value


def main() -> int:
    arguments = parse_arguments()
    profile = load_json(arguments.profile)
    if profile.get("schema_version") != PROFILE_SCHEMA:
        raise ValueError("Unsupported training profile schema")
    dataset_manifest = load_json(arguments.dataset / "vehicle_conversion_manifest.json")
    if dataset_manifest.get("schema_version") != DATASET_SCHEMA:
        raise ValueError("Unsupported converted dataset schema")
    mapping = dataset_manifest["mapping"]
    features = dataset_manifest["features"]
    base_config = load_json(arguments.base_model / "config.json")
    preprocessor = load_json(arguments.base_model / "policy_preprocessor.json")
    vlm_manifest = load_json(arguments.vlm_manifest)

    revision = vlm_manifest["resolved_revision"]
    repo_cache_name = "models--" + vlm_manifest["repo_id"].replace("/", "--")
    host_vlm_cache = (arguments.hf_home / "hub" / repo_cache_name).resolve()
    host_vlm_snapshot = host_vlm_cache / "snapshots" / revision
    if not (host_vlm_snapshot / "config.json").exists():
        raise FileNotFoundError(f"Missing VLM config: {host_vlm_snapshot}")
    if not (host_vlm_snapshot / "model.safetensors").exists():
        raise FileNotFoundError(f"Missing VLM weights: {host_vlm_snapshot}")

    input_features = {}
    for image_mapping in mapping["image_features"]:
        key = image_mapping["target_key"]
        height, width, channels = features[key]["shape"]
        input_features[key] = {
            "type": "VISUAL",
            "shape": [channels, height, width],
        }
    state_key = mapping["state_feature"]["target_key"]
    input_features[state_key] = {
        "type": "STATE",
        "shape": features[state_key]["shape"],
    }
    action_key = mapping["action_feature"]["target_key"]
    output_features = {
        action_key: {
            "type": "ACTION",
            "shape": features[action_key]["shape"],
        }
    }

    container_vlm_snapshot = f"/models/smolvlm-cache/snapshots/{revision}"
    base_config.update(
        {
            "input_features": input_features,
            "output_features": output_features,
            "device": "cuda",
            "use_amp": require_boolean(profile, "use_amp"),
            "push_to_hub": False,
            "repo_id": None,
            "resize_imgs_with_padding": profile["resize_imgs_with_padding"],
            "freeze_vision_encoder": require_boolean(profile, "freeze_vision_encoder"),
            "train_expert_only": require_boolean(profile, "train_expert_only"),
            "train_state_proj": require_boolean(profile, "train_state_proj"),
            "vlm_model_name": container_vlm_snapshot,
        }
    )
    for step in preprocessor["steps"]:
        if step.get("registry_name") == "tokenizer_processor":
            step["config"]["tokenizer_name"] = container_vlm_snapshot

    output = arguments.output.resolve()
    if output.exists():
        raise FileExistsError(f"Overlay output already exists: {output}")
    output.mkdir(parents=True)
    (output / "config.json").write_text(json.dumps(base_config, indent=2) + "\n")
    (output / "policy_preprocessor.json").write_text(json.dumps(preprocessor, indent=2) + "\n")

    runtime = {
        "schema_version": "vehicle.training.runtime.v1",
        "profile": profile,
        "dataset_repo_id": dataset_manifest["repo_id"],
        "dataset_episode_count": dataset_manifest["episode_count"],
        "dataset_frame_count": dataset_manifest["frame_count"],
        "host_vlm_cache": str(host_vlm_cache),
        "vlm_revision": revision,
        "policy_input_features": input_features,
        "policy_output_features": output_features,
    }
    (output / "runtime.json").write_text(json.dumps(runtime, indent=2) + "\n")
    print(output / "runtime.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())