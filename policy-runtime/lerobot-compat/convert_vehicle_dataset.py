#!/usr/bin/env python3

import argparse
import json
import math
import shutil
import statistics
from pathlib import Path

import numpy as np
from PIL import Image

from lerobot.datasets.lerobot_dataset import LeRobotDataset


MAPPING_SCHEMA = "vehicle.lerobot.mapping.v1"
DATASET_SCHEMA = "vehicle.dataset.v1"
FRAME_SCHEMA = "vehicle.dataset.frame.v1"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert vehicle.dataset.v1 episodes into a local LeRobot dataset."
    )
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--input", required=True, action="append", type=Path)
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def load_mapping(path: Path) -> dict:
    mapping = load_json(path)
    if mapping.get("schema_version") != MAPPING_SCHEMA:
        raise ValueError(f"Unsupported mapping schema in {path}")
    if not isinstance(mapping.get("fps"), int) or mapping["fps"] <= 0:
        raise ValueError("fps must be a positive integer")
    tolerance = float(mapping.get("fps_tolerance_ratio", 0.2))
    if tolerance < 0.0 or tolerance >= 1.0:
        raise ValueError("fps_tolerance_ratio must be in [0, 1)")
    if not mapping.get("repo_id"):
        raise ValueError("repo_id is required")
    if not mapping.get("image_features"):
        raise ValueError("At least one image feature is required")
    if not mapping.get("state_feature", {}).get("keys"):
        raise ValueError("state_feature.keys is required")
    if mapping.get("action_feature", {}).get("source") != "target.twist":
        raise ValueError("Only action source target.twist is currently supported")
    if not mapping.get("action_feature", {}).get("keys"):
        raise ValueError("action_feature.keys is required")
    return mapping


def load_episode(path: Path) -> tuple[dict, list[dict]]:
    manifest_path = path / "dataset_manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Missing {manifest_path}")
    manifest = load_json(manifest_path)
    if manifest.get("schema_version") != DATASET_SCHEMA:
        raise ValueError(f"Unsupported dataset schema in {manifest_path}")

    frame_path = path / manifest.get("frame_file", "frames.jsonl")
    frames = []
    with frame_path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            if not line.strip():
                continue
            frame = json.loads(line)
            if frame.get("schema_version") != FRAME_SCHEMA:
                raise ValueError(f"Unsupported frame schema at {frame_path}:{line_number}")
            frames.append(frame)
    if not frames:
        raise ValueError(f"Episode contains no frames: {path}")
    return manifest, frames


def validate_episode_timing(frames: list[dict], mapping: dict, episode_path: Path) -> float:
    if len(frames) < 2:
        return float(mapping["fps"])
    timestamps = [int(frame["bag_timestamp_ns"]) for frame in frames]
    deltas = [
        (right - left) / 1_000_000_000
        for left, right in zip(timestamps, timestamps[1:])
    ]
    if any(delta <= 0.0 for delta in deltas):
        raise ValueError(f"Non-increasing frame timestamps in {episode_path}")
    median_delta = statistics.median(deltas)
    expected_delta = 1.0 / mapping["fps"]
    relative_error = abs(median_delta - expected_delta) / expected_delta
    tolerance = float(mapping.get("fps_tolerance_ratio", 0.2))
    if relative_error > tolerance:
        measured_fps = 1.0 / median_delta
        raise ValueError(
            f"Episode {episode_path} is {measured_fps:.3f} FPS but mapping declares "
            f"{mapping['fps']} FPS; adjust the mapping or resample explicitly"
        )
    return 1.0 / median_delta


def find_image(frame: dict, source_key: str, episode_path: Path) -> Path:
    for image in frame.get("images", []):
        if image.get("key") == source_key:
            image_path = (episode_path / image["path"]).resolve()
            episode_root = episode_path.resolve()
            if image_path != episode_root and episode_root not in image_path.parents:
                raise ValueError(f"Image escapes episode directory: {image_path}")
            if not image_path.is_file():
                raise FileNotFoundError(image_path)
            return image_path
    raise ValueError(f"Frame {frame.get('frame_index')} has no image {source_key}")


def build_features(mapping: dict, first_episode: Path, first_frame: dict) -> dict:
    features = {}
    for image_mapping in mapping["image_features"]:
        image_path = find_image(first_frame, image_mapping["source_key"], first_episode)
        with Image.open(image_path) as image:
            width, height = image.size
        features[image_mapping["target_key"]] = {
            "dtype": "image",
            "shape": (height, width, 3),
            "names": ["height", "width", "channels"],
        }

    state_mapping = mapping["state_feature"]
    state_names = state_mapping["keys"]
    features[state_mapping["target_key"]] = {
        "dtype": "float32",
        "shape": (len(state_names),),
        "names": state_names,
    }
    validity_key = state_mapping.get("validity_target_key")
    if validity_key:
        features[validity_key] = {
            "dtype": "float32",
            "shape": (len(state_names),),
            "names": state_names,
        }

    action_mapping = mapping["action_feature"]
    features[action_mapping["target_key"]] = {
        "dtype": "float32",
        "shape": (len(action_mapping["keys"]),),
        "names": action_mapping["keys"],
    }
    features["source.timestamp_ns"] = {
        "dtype": "int64",
        "shape": (1,),
        "names": ["bag_timestamp_ns"],
    }
    features["source.frame_index"] = {
        "dtype": "int64",
        "shape": (1,),
        "names": ["vehicle_frame_index"],
    }
    return features


def finite_float(value, label: str) -> float:
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"Non-finite value for {label}: {value}")
    return result


def convert_frame(frame: dict, episode_path: Path, mapping: dict) -> dict:
    converted = {
        "task": frame.get("task") or "vehicle operation",
        "source.timestamp_ns": np.asarray(
            [int(frame["bag_timestamp_ns"])], dtype=np.int64
        ),
        "source.frame_index": np.asarray(
            [int(frame["frame_index"])], dtype=np.int64
        ),
    }

    for image_mapping in mapping["image_features"]:
        image_path = find_image(frame, image_mapping["source_key"], episode_path)
        with Image.open(image_path) as image:
            converted[image_mapping["target_key"]] = np.asarray(image.convert("RGB"))

    state_by_key = {item["key"]: item for item in frame.get("state", [])}
    state_mapping = mapping["state_feature"]
    invalid_fill = finite_float(state_mapping.get("invalid_fill", 0.0), "invalid_fill")
    state_values = []
    state_validity = []
    for state_key in state_mapping["keys"]:
        state = state_by_key.get(state_key)
        valid = bool(state and state.get("valid", False))
        state_values.append(
            finite_float(state["value"], state_key) if valid else invalid_fill
        )
        state_validity.append(1.0 if valid else 0.0)
    converted[state_mapping["target_key"]] = np.asarray(state_values, dtype=np.float32)
    validity_key = state_mapping.get("validity_target_key")
    if validity_key:
        converted[validity_key] = np.asarray(state_validity, dtype=np.float32)

    target = frame.get("target", {})
    if not target.get("available", False):
        raise ValueError(f"Frame {frame.get('frame_index')} has no training target")
    twist = target.get("twist", {})
    action_mapping = mapping["action_feature"]
    converted[action_mapping["target_key"]] = np.asarray(
        [finite_float(twist[key], f"target.twist.{key}") for key in action_mapping["keys"]],
        dtype=np.float32,
    )
    return converted


def main() -> int:
    arguments = parse_arguments()
    mapping = load_mapping(arguments.config)
    output_path = arguments.output.resolve()
    temporary_path = output_path.with_name(output_path.name + ".partial")
    if output_path.exists():
        raise FileExistsError(f"Output path already exists: {output_path}")
    if temporary_path.exists():
        raise FileExistsError(f"Temporary path already exists: {temporary_path}")

    episodes = []
    for input_path in arguments.input:
        episode_path = input_path.resolve()
        manifest, frames = load_episode(episode_path)
        measured_fps = validate_episode_timing(frames, mapping, episode_path)
        episodes.append((episode_path, manifest, frames, measured_fps))

    features = build_features(mapping, episodes[0][0], episodes[0][2][0])
    conversion_episodes = []
    try:
        dataset = LeRobotDataset.create(
            repo_id=mapping["repo_id"],
            fps=mapping["fps"],
            features=features,
            root=temporary_path,
            robot_type=mapping.get("robot_type", "ackermann"),
            use_videos=bool(mapping.get("use_videos", False)),
        )
        for episode_path, manifest, frames, measured_fps in episodes:
            for frame in frames:
                dataset.add_frame(convert_frame(frame, episode_path, mapping))
            dataset.save_episode(parallel_encoding=False)
            conversion_episodes.append(
                {
                    "source_episode_id": manifest.get("source_episode_id"),
                    "frame_count": len(frames),
                    "measured_fps": measured_fps,
                }
            )
        dataset.finalize()
        conversion_manifest = {
            "schema_version": "vehicle.lerobot.conversion.v1",
            "mapping_schema_version": mapping["schema_version"],
            "repo_id": mapping["repo_id"],
            "fps": mapping["fps"],
            "robot_type": mapping.get("robot_type", "ackermann"),
            "episode_count": len(conversion_episodes),
            "frame_count": sum(item["frame_count"] for item in conversion_episodes),
            "features": features,
            "mapping": mapping,
            "episodes": conversion_episodes,
        }
        with (temporary_path / "vehicle_conversion_manifest.json").open(
            "w", encoding="utf-8"
        ) as stream:
            json.dump(conversion_manifest, stream, indent=2)
            stream.write("\n")
        temporary_path.rename(output_path)
    except Exception:
        shutil.rmtree(temporary_path, ignore_errors=True)
        raise

    print(f"repo_id={mapping['repo_id']}")
    print(f"episode_count={len(conversion_episodes)}")
    print(f"frame_count={sum(item['frame_count'] for item in conversion_episodes)}")
    print(f"output={output_path}")
    print("LEROBOT_CONVERSION_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())