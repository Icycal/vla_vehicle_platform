#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

import numpy as np

from lerobot.datasets.lerobot_dataset import LeRobotDataset


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify a converted local LeRobot dataset.")
    parser.add_argument("--dataset", required=True, type=Path)
    return parser.parse_args()


def expected_shape(feature: dict) -> tuple[int, ...]:
    return tuple(int(value) for value in feature["shape"])


def main() -> int:
    arguments = parse_arguments()
    root = arguments.dataset.resolve()
    manifest_path = root / "vehicle_conversion_manifest.json"
    with manifest_path.open("r", encoding="utf-8") as stream:
        manifest = json.load(stream)

    mapping = manifest["mapping"]
    dataset = LeRobotDataset(repo_id=manifest["repo_id"], root=root)
    if dataset.num_episodes != manifest["episode_count"]:
        raise ValueError("Episode count differs from conversion manifest")
    if len(dataset) != manifest["frame_count"]:
        raise ValueError("Frame count differs from conversion manifest")

    image_keys = [item["target_key"] for item in mapping["image_features"]]
    state_key = mapping["state_feature"]["target_key"]
    validity_key = mapping["state_feature"].get("validity_target_key")
    action_key = mapping["action_feature"]["target_key"]
    required_features = {
        *image_keys,
        state_key,
        action_key,
        "source.timestamp_ns",
        "source.frame_index",
    }
    if validity_key:
        required_features.add(validity_key)
    missing = required_features.difference(dataset.features)
    if missing:
        raise ValueError(f"Missing required features: {sorted(missing)}")

    for index in sorted({0, len(dataset) - 1}):
        sample = dataset[index]
        state = np.asarray(sample[state_key])
        action = np.asarray(sample[action_key])
        if state.shape != expected_shape(manifest["features"][state_key]):
            raise ValueError(f"Unexpected state shape: {state.shape}")
        if action.shape != expected_shape(manifest["features"][action_key]):
            raise ValueError(f"Unexpected action shape: {action.shape}")
        if validity_key:
            validity = np.asarray(sample[validity_key])
            if validity.shape != expected_shape(manifest["features"][validity_key]):
                raise ValueError(f"Unexpected validity shape: {validity.shape}")
        for image_key in image_keys:
            image = np.asarray(sample[image_key])
            if image.ndim != 3:
                raise ValueError(f"Unexpected image rank for {image_key}: {image.shape}")
        if not np.isfinite(state).all() or not np.isfinite(action).all():
            raise ValueError("Dataset contains non-finite state or action")

    print(f"repo_id={manifest['repo_id']}")
    print(f"episode_count={dataset.num_episodes}")
    print(f"frame_count={len(dataset)}")
    print(f"features={','.join(sorted(dataset.features))}")
    print("LEROBOT_DATASET_VERIFY_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())