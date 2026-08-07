#!/usr/bin/env python3

import argparse
import hashlib
import json
import math
import shutil
import statistics
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from PIL import Image


REPORT_SCHEMA = "vehicle.dataset.quality.v1"
VEHICLE_SCHEMA = "vehicle.dataset.v1"
LEROBOT_SCHEMA = "vehicle.lerobot.conversion.v1"


class NumericStats:
    def __init__(self) -> None:
        self.values = []
        self.nonfinite_count = 0
        self.zero_count = 0

    def add(self, value, zero_epsilon: float) -> None:
        number = float(value)
        if not math.isfinite(number):
            self.nonfinite_count += 1
            return
        self.values.append(number)
        if abs(number) <= zero_epsilon:
            self.zero_count += 1

    def report(self) -> dict:
        if not self.values:
            return {
                "count": 0,
                "nonfinite_count": self.nonfinite_count,
                "minimum": None,
                "maximum": None,
                "mean": None,
                "standard_deviation": None,
                "zero_ratio": None,
            }
        return {
            "count": len(self.values),
            "nonfinite_count": self.nonfinite_count,
            "minimum": min(self.values),
            "maximum": max(self.values),
            "mean": statistics.fmean(self.values),
            "standard_deviation": statistics.pstdev(self.values),
            "zero_ratio": self.zero_count / len(self.values),
        }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inspect vehicle or native LeRobot datasets.")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--max-image-checks", type=int, default=200)
    parser.add_argument("--zero-epsilon", type=float, default=1e-6)
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def add_issue(issues: list[dict], severity: str, code: str, message: str) -> None:
    issues.append({"severity": severity, "code": code, "message": message})


def select_indices(length: int, maximum: int) -> list[int]:
    if length <= 0 or maximum <= 0:
        return []
    if length <= maximum:
        return list(range(length))
    return sorted({round(index * (length - 1) / (maximum - 1)) for index in range(maximum)})


def timing_report(timestamps_ns: list[int], issues: list[dict]) -> dict:
    if len(timestamps_ns) < 2:
        return {"sample_count": len(timestamps_ns), "measured_fps": None}
    deltas = [
        (right - left) / 1_000_000_000
        for left, right in zip(timestamps_ns, timestamps_ns[1:])
    ]
    nonpositive = sum(delta <= 0.0 for delta in deltas)
    if nonpositive:
        add_issue(
            issues,
            "error",
            "non_increasing_timestamps",
            f"Found {nonpositive} non-increasing frame intervals.",
        )
    positive = [delta for delta in deltas if delta > 0.0]
    if not positive:
        return {"sample_count": len(timestamps_ns), "measured_fps": None}
    median_delta = statistics.median(positive)
    return {
        "sample_count": len(timestamps_ns),
        "duration_seconds": (timestamps_ns[-1] - timestamps_ns[0]) / 1_000_000_000,
        "median_delta_seconds": median_delta,
        "minimum_delta_seconds": min(positive),
        "maximum_delta_seconds": max(positive),
        "measured_fps": 1.0 / median_delta,
        "nonpositive_interval_count": nonpositive,
    }


def image_file_report(
    records: list[tuple[str, Path]],
    expected_shapes: dict[str, tuple[int, int]],
    issues: list[dict],
) -> dict:
    per_key = defaultdict(
        lambda: {
            "checked_count": 0,
            "missing_count": 0,
            "corrupt_count": 0,
            "shape_mismatch_count": 0,
            "hashes": [],
            "dimensions": Counter(),
        }
    )
    for key, path in records:
        stats = per_key[key]
        stats["checked_count"] += 1
        if not path.is_file():
            stats["missing_count"] += 1
            continue
        try:
            with Image.open(path) as image:
                image.verify()
            with Image.open(path) as image:
                width, height = image.size
            stats["dimensions"][(height, width)] += 1
            expected = expected_shapes.get(key)
            if expected is None:
                expected_shapes[key] = (height, width)
            elif (height, width) != expected:
                stats["shape_mismatch_count"] += 1
            stats["hashes"].append(hashlib.sha256(path.read_bytes()).hexdigest())
        except Exception:
            stats["corrupt_count"] += 1

    report = {}
    for key, stats in sorted(per_key.items()):
        duplicate_count = len(stats["hashes"]) - len(set(stats["hashes"]))
        report[key] = {
            "checked_count": stats["checked_count"],
            "missing_count": stats["missing_count"],
            "corrupt_count": stats["corrupt_count"],
            "shape_mismatch_count": stats["shape_mismatch_count"],
            "duplicate_count": duplicate_count,
            "dimensions": {
                f"{height}x{width}": count
                for (height, width), count in sorted(stats["dimensions"].items())
            },
        }
        if stats["missing_count"]:
            add_issue(issues, "error", "missing_images", f"{key} has missing image files.")
        if stats["corrupt_count"]:
            add_issue(issues, "error", "corrupt_images", f"{key} has corrupt image files.")
        if stats["shape_mismatch_count"]:
            add_issue(issues, "error", "image_shape_mismatch", f"{key} changes image shape.")
        if stats["checked_count"] and duplicate_count / stats["checked_count"] > 0.5:
            add_issue(
                issues,
                "warning",
                "high_duplicate_image_ratio",
                f"More than half of checked {key} images are byte-identical.",
            )
    return report


def finalize_report(report: dict, issues: list[dict]) -> dict:
    report["issues"] = issues
    report["issue_counts"] = dict(Counter(issue["severity"] for issue in issues))
    report["training_readiness"] = not any(
        issue["severity"] in {"blocker", "error"} for issue in issues
    )
    return report


def inspect_vehicle(path: Path, max_image_checks: int, zero_epsilon: float) -> dict:
    issues = []
    manifest = load_json(path / "dataset_manifest.json")
    if manifest.get("schema_version") != VEHICLE_SCHEMA:
        raise ValueError("Unsupported vehicle dataset schema")
    frame_path = path / manifest.get("frame_file", "frames.jsonl")
    frames = [json.loads(line) for line in frame_path.read_text().splitlines() if line.strip()]
    if not frames:
        add_issue(issues, "error", "empty_dataset", "Dataset contains no frames.")

    tasks = Counter(frame.get("task") or "" for frame in frames)
    timestamps = [int(frame["bag_timestamp_ns"]) for frame in frames]
    state_stats = defaultdict(NumericStats)
    state_valid = Counter()
    state_present = Counter()
    action_stats = {"linear_x": NumericStats(), "angular_z": NumericStats()}
    zero_action_frames = 0
    missing_targets = 0
    for frame in frames:
        for state in frame.get("state", []):
            key = state["key"]
            state_present[key] += 1
            if state.get("valid", False):
                state_valid[key] += 1
                state_stats[key].add(state["value"], zero_epsilon)
        target = frame.get("target", {})
        if not target.get("available", False):
            missing_targets += 1
            continue
        twist = target.get("twist", {})
        values = [float(twist.get("linear_x", math.nan)), float(twist.get("angular_z", math.nan))]
        for key, value in zip(action_stats, values):
            action_stats[key].add(value, zero_epsilon)
        if all(math.isfinite(value) and abs(value) <= zero_epsilon for value in values):
            zero_action_frames += 1

    state_report = {}
    for key in sorted(state_present):
        valid_count = state_valid[key]
        state_report[key] = {
            **state_stats[key].report(),
            "present_count": state_present[key],
            "valid_count": valid_count,
            "valid_ratio": valid_count / len(frames) if frames else None,
        }
        if valid_count == 0:
            add_issue(issues, "blocker", "state_never_valid", f"{key} is never valid.")
        elif valid_count / len(frames) < 0.8:
            add_issue(issues, "warning", "low_state_validity", f"{key} validity is below 80%.")
        if state_stats[key].nonfinite_count:
            add_issue(issues, "error", "nonfinite_state", f"{key} contains non-finite values.")

    if missing_targets:
        add_issue(issues, "error", "missing_training_targets", f"{missing_targets} frames lack targets.")
    target_count = len(frames) - missing_targets
    zero_action_ratio = zero_action_frames / target_count if target_count else None
    if zero_action_ratio is not None and zero_action_ratio >= 0.95:
        add_issue(issues, "blocker", "degenerate_actions", "At least 95% of actions are zero.")
    if any(stats.nonfinite_count for stats in action_stats.values()):
        add_issue(issues, "error", "nonfinite_action", "Actions contain non-finite values.")
    if len(frames) and len(tasks) == 1:
        add_issue(issues, "warning", "single_task", "Dataset contains only one task string.")
    add_issue(issues, "blocker", "insufficient_episodes", "A single vehicle export is only one Episode.")

    sampled_indices = select_indices(len(frames), max_image_checks)
    expected_shapes = {}
    image_records = []
    for index in sampled_indices:
        for image in frames[index].get("images", []):
            key = image["key"]
            image_records.append((key, (path / image["path"]).resolve()))
    image_report = image_file_report(image_records, expected_shapes, issues)

    return finalize_report(
        {
            "schema_version": REPORT_SCHEMA,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "dataset_type": "vehicle.dataset.v1",
            "source": str(path),
            "episode_count": 1,
            "frame_count": len(frames),
            "tasks": dict(sorted(tasks.items())),
            "timing": timing_report(timestamps, issues),
            "images": image_report,
            "state": state_report,
            "action": {
                "features": {key: stats.report() for key, stats in action_stats.items()},
                "target_count": target_count,
                "missing_target_count": missing_targets,
                "zero_action_frame_count": zero_action_frames,
                "zero_action_ratio": zero_action_ratio,
            },
        },
        issues,
    )


def inspect_lerobot(path: Path, max_image_checks: int, zero_epsilon: float) -> dict:
    from lerobot.datasets.lerobot_dataset import LeRobotDataset

    issues = []
    manifest = load_json(path / "vehicle_conversion_manifest.json")
    if manifest.get("schema_version") != LEROBOT_SCHEMA:
        raise ValueError("Unsupported LeRobot conversion schema")
    dataset = LeRobotDataset(repo_id=manifest["repo_id"], root=path)
    ensure_loaded = getattr(dataset, "_ensure_hf_dataset_loaded", None)
    if callable(ensure_loaded):
        ensure_loaded()
    if dataset.hf_dataset is None:
        raise RuntimeError("LeRobotDataset did not load its Hugging Face dataset")
    mapping = manifest["mapping"]
    state_key = mapping["state_feature"]["target_key"]
    validity_key = mapping["state_feature"].get("validity_target_key")
    action_key = mapping["action_feature"]["target_key"]
    image_keys = [entry["target_key"] for entry in mapping["image_features"]]
    numeric_columns = [state_key, action_key, "source.timestamp_ns", "episode_index", "task_index"]
    if validity_key:
        numeric_columns.append(validity_key)
    numeric_dataset = dataset.hf_dataset.select_columns(numeric_columns)

    state_names = manifest["features"][state_key]["names"]
    action_names = manifest["features"][action_key]["names"]
    state_stats = {name: NumericStats() for name in state_names}
    state_valid = Counter()
    action_stats = {name: NumericStats() for name in action_names}
    zero_action_frames = 0
    timestamps_by_episode = defaultdict(list)
    tasks = Counter()
    episodes = Counter()
    for row in numeric_dataset:
        state = np.asarray(row[state_key])
        validity = np.asarray(row[validity_key]) if validity_key else np.ones_like(state)
        action = np.asarray(row[action_key])
        for index, name in enumerate(state_names):
            if validity[index] > 0.5:
                state_valid[name] += 1
                state_stats[name].add(state[index], zero_epsilon)
        for index, name in enumerate(action_names):
            action_stats[name].add(action[index], zero_epsilon)
        if np.isfinite(action).all() and np.all(np.abs(action) <= zero_epsilon):
            zero_action_frames += 1
        episode_index = str(int(row["episode_index"]))
        timestamps_by_episode[episode_index].append(
            int(np.asarray(row["source.timestamp_ns"]).reshape(-1)[0])
        )
        tasks[str(int(row["task_index"]))] += 1
        episodes[episode_index] += 1

    frame_count = len(dataset)
    state_report = {}
    for name in state_names:
        valid_count = state_valid[name]
        state_report[name] = {
            **state_stats[name].report(),
            "valid_count": valid_count,
            "valid_ratio": valid_count / frame_count if frame_count else None,
        }
        if valid_count == 0:
            add_issue(issues, "blocker", "state_never_valid", f"{name} is never valid.")
        elif valid_count / frame_count < 0.8:
            add_issue(issues, "warning", "low_state_validity", f"{name} validity is below 80%.")
        if state_stats[name].nonfinite_count:
            add_issue(issues, "error", "nonfinite_state", f"{name} contains non-finite values.")

    zero_action_ratio = zero_action_frames / frame_count if frame_count else None
    if zero_action_ratio is not None and zero_action_ratio >= 0.95:
        add_issue(issues, "blocker", "degenerate_actions", "At least 95% of actions are zero.")
    if any(stats.nonfinite_count for stats in action_stats.values()):
        add_issue(issues, "error", "nonfinite_action", "Actions contain non-finite values.")
    if manifest["episode_count"] < 3:
        add_issue(issues, "blocker", "insufficient_episodes", "Fewer than three Episodes are available.")
    if len(tasks) == 1:
        add_issue(issues, "warning", "single_task", "Dataset contains only one task index.")

    image_report = {}
    sampled_indices = select_indices(frame_count, max_image_checks)
    image_hashes = defaultdict(list)
    image_shapes = defaultdict(Counter)
    image_errors = Counter()
    for index in sampled_indices:
        sample = dataset[index]
        for key in image_keys:
            try:
                image = np.asarray(sample[key])
                image_shapes[key][tuple(image.shape)] += 1
                image_hashes[key].append(hashlib.sha256(image.tobytes()).hexdigest())
            except Exception:
                image_errors[key] += 1
    for key in image_keys:
        duplicate_count = len(image_hashes[key]) - len(set(image_hashes[key]))
        image_report[key] = {
            "checked_count": len(sampled_indices),
            "decode_error_count": image_errors[key],
            "duplicate_count": duplicate_count,
            "shapes": {"x".join(map(str, shape)): count for shape, count in image_shapes[key].items()},
        }
        if image_errors[key]:
            add_issue(issues, "error", "image_decode_error", f"{key} cannot be decoded.")
        if len(image_shapes[key]) > 1:
            add_issue(issues, "error", "image_shape_mismatch", f"{key} changes image shape.")
        if sampled_indices and duplicate_count / len(sampled_indices) > 0.5:
            add_issue(
                issues,
                "warning",
                "high_duplicate_image_ratio",
                f"More than half of checked {key} images are identical.",
            )

    timing_by_episode = {
        episode_index: timing_report(timestamps, issues)
        for episode_index, timestamps in sorted(timestamps_by_episode.items())
    }
    measured_fps = [
        timing["measured_fps"]
        for timing in timing_by_episode.values()
        if timing.get("measured_fps") is not None
    ]
    timing = {
        "episodes": timing_by_episode,
        "median_measured_fps": statistics.median(measured_fps) if measured_fps else None,
    }

    return finalize_report(
        {
            "schema_version": REPORT_SCHEMA,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "dataset_type": "lerobot.v3",
            "source": str(path),
            "repo_id": manifest["repo_id"],
            "episode_count": manifest["episode_count"],
            "frame_count": frame_count,
            "tasks_by_index": dict(sorted(tasks.items())),
            "frames_by_episode_index": dict(sorted(episodes.items())),
            "timing": timing,
            "images": image_report,
            "state": state_report,
            "action": {
                "features": {key: stats.report() for key, stats in action_stats.items()},
                "zero_action_frame_count": zero_action_frames,
                "zero_action_ratio": zero_action_ratio,
            },
        },
        issues,
    )


def markdown_report(report: dict) -> str:
    lines = [
        "# Dataset Quality Report",
        "",
        f"- Dataset type: `{report['dataset_type']}`",
        f"- Episodes: {report['episode_count']}",
        f"- Frames: {report['frame_count']}",
        f"- Training ready: **{str(report['training_readiness']).lower()}**",
        "",
        "## Issues",
        "",
    ]
    if report["issues"]:
        for issue in report["issues"]:
            lines.append(f"- **{issue['severity']} / {issue['code']}**: {issue['message']}")
    else:
        lines.append("- No issues detected.")
    lines.extend(["", "## Timing", "", "```json", json.dumps(report["timing"], indent=2), "```"])
    lines.extend(["", "## Action", "", "```json", json.dumps(report["action"], indent=2), "```"])
    lines.extend(["", "## State Validity", ""])
    for key, stats in report["state"].items():
        lines.append(f"- `{key}`: valid_ratio={stats.get('valid_ratio')}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    arguments = parse_arguments()
    dataset = arguments.dataset.resolve()
    output = arguments.output.resolve()
    temporary = output.with_name(output.name + ".partial")
    if output.exists():
        raise FileExistsError(f"Output path already exists: {output}")
    if temporary.exists():
        raise FileExistsError(f"Temporary path already exists: {temporary}")
    if (dataset / "vehicle_conversion_manifest.json").is_file():
        report = inspect_lerobot(dataset, arguments.max_image_checks, arguments.zero_epsilon)
    elif (dataset / "dataset_manifest.json").is_file():
        report = inspect_vehicle(dataset, arguments.max_image_checks, arguments.zero_epsilon)
    else:
        raise ValueError("Dataset type cannot be detected")

    try:
        temporary.mkdir(parents=True)
        (temporary / "dataset_quality_report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8"
        )
        (temporary / "dataset_quality_summary.md").write_text(
            markdown_report(report), encoding="utf-8"
        )
        temporary.rename(output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    print(f"dataset_type={report['dataset_type']}")
    print(f"episode_count={report['episode_count']}")
    print(f"frame_count={report['frame_count']}")
    print(f"training_readiness={str(report['training_readiness']).lower()}")
    print(f"output={output}")
    print("DATASET_INSPECTION_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
