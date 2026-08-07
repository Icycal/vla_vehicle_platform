#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import random
from collections import Counter
from pathlib import Path


DATASET_SCHEMA = "vehicle.dataset.v1"
SPLIT_SCHEMA = "vehicle.dataset.split.v1"
SPLIT_NAMES = ("train", "validation", "test")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a deterministic Episode-level split manifest."
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=1000)
    parser.add_argument("--train-ratio", type=float, default=0.8)
    parser.add_argument("--validation-ratio", type=float, default=0.1)
    parser.add_argument("--test-ratio", type=float, default=0.1)
    parser.add_argument("datasets", nargs="+", type=Path)
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def portable_path(path: Path, base: Path) -> str:
    return Path(os.path.relpath(path, base)).as_posix()


def inspect_episode(path: Path, output_parent: Path) -> dict:
    dataset_path = path.resolve()
    manifest_path = dataset_path / "dataset_manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Missing {manifest_path}")
    manifest = load_json(manifest_path)
    if manifest.get("schema_version") != DATASET_SCHEMA:
        raise ValueError(f"Unsupported dataset schema in {manifest_path}")

    episode_id = str(manifest.get("source_episode_id") or "").strip()
    if not episode_id:
        raise ValueError(f"Missing source_episode_id in {manifest_path}")
    frame_path = dataset_path / manifest.get("frame_file", "frames.jsonl")
    if not frame_path.is_file():
        raise FileNotFoundError(f"Missing {frame_path}")

    frame_count = 0
    task_counts = Counter()
    with frame_path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            if not line.strip():
                continue
            try:
                frame = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"Invalid JSON at {frame_path}:{line_number}") from error
            frame_count += 1
            task_counts[str(frame.get("task") or "<empty>")] += 1
    if frame_count == 0:
        raise ValueError(f"Episode contains no frames: {dataset_path}")
    manifest_frame_count = manifest.get("frame_count")
    if manifest_frame_count is not None and int(manifest_frame_count) != frame_count:
        raise ValueError(
            f"Frame count mismatch for {episode_id}: "
            f"manifest={manifest_frame_count} actual={frame_count}"
        )

    return {
        "episode_id": episode_id,
        "path": portable_path(dataset_path, output_parent),
        "frame_count": frame_count,
        "task_counts": dict(sorted(task_counts.items())),
        "manifest_sha256": sha256_file(manifest_path),
        "frames_sha256": sha256_file(frame_path),
    }


def allocate_counts(episode_count: int, ratios: dict[str, float]) -> dict[str, int]:
    if any(value < 0.0 for value in ratios.values()):
        raise ValueError("Split ratios cannot be negative")
    if ratios["train"] <= 0.0:
        raise ValueError("train ratio must be greater than zero")
    ratio_sum = sum(ratios.values())
    if ratio_sum <= 0.0:
        raise ValueError("At least one split ratio must be greater than zero")
    if episode_count == 1:
        return {"train": 1, "validation": 0, "test": 0}
    if episode_count == 2:
        non_train = [name for name in ("validation", "test") if ratios[name] > 0.0]
        if not non_train:
            return {"train": 2, "validation": 0, "test": 0}
        secondary = max(
            non_train,
            key=lambda name: (ratios[name], -SPLIT_NAMES.index(name)),
        )
        counts = {name: 0 for name in SPLIT_NAMES}
        counts["train"] = 1
        counts[secondary] = 1
        return counts

    normalized = {name: ratios[name] / ratio_sum for name in SPLIT_NAMES}
    raw_counts = {name: normalized[name] * episode_count for name in SPLIT_NAMES}
    counts = {name: int(raw_counts[name]) for name in SPLIT_NAMES}
    remaining = episode_count - sum(counts.values())
    order = sorted(
        SPLIT_NAMES,
        key=lambda name: (raw_counts[name] - counts[name], ratios[name], -SPLIT_NAMES.index(name)),
        reverse=True,
    )
    for name in order[:remaining]:
        counts[name] += 1

    active = [name for name in SPLIT_NAMES if ratios[name] > 0.0]
    minimums = {name: 0 for name in SPLIT_NAMES}
    minimums["train"] = 1 if episode_count else 0
    if episode_count >= len(active):
        for name in active:
            minimums[name] = 1

    for receiver in SPLIT_NAMES:
        while counts[receiver] < minimums[receiver]:
            donors = [
                name for name in SPLIT_NAMES if counts[name] > minimums[name]
            ]
            if not donors:
                break
            donor = max(
                donors,
                key=lambda name: (counts[name] - minimums[name], counts[name]),
            )
            counts[donor] -= 1
            counts[receiver] += 1
    return counts


def summarize(entries: list[dict]) -> dict:
    tasks = Counter()
    for entry in entries:
        tasks.update(entry["task_counts"])
    return {
        "episode_count": len(entries),
        "frame_count": sum(entry["frame_count"] for entry in entries),
        "task_counts": dict(sorted(tasks.items())),
    }


def main() -> int:
    arguments = parse_arguments()
    output = arguments.output.resolve()
    temporary = output.with_name(output.name + ".partial")
    if output.exists():
        raise FileExistsError(f"Output path already exists: {output}")
    if temporary.exists():
        raise FileExistsError(f"Temporary path already exists: {temporary}")
    output.parent.mkdir(parents=True, exist_ok=True)

    episodes = [inspect_episode(path, output.parent) for path in arguments.datasets]
    episode_ids = [episode["episode_id"] for episode in episodes]
    duplicates = sorted(
        episode_id for episode_id, count in Counter(episode_ids).items() if count > 1
    )
    if duplicates:
        raise ValueError(f"Duplicate Episode IDs: {', '.join(duplicates)}")

    ratios = {
        "train": arguments.train_ratio,
        "validation": arguments.validation_ratio,
        "test": arguments.test_ratio,
    }
    counts = allocate_counts(len(episodes), ratios)
    episodes.sort(key=lambda episode: episode["episode_id"])
    random.Random(arguments.seed).shuffle(episodes)

    splits = {}
    offset = 0
    for name in SPLIT_NAMES:
        split_entries = episodes[offset : offset + counts[name]]
        splits[name] = split_entries
        offset += counts[name]

    warnings = []
    for name in ("validation", "test"):
        if ratios[name] > 0.0 and not splits[name]:
            warnings.append(
                {
                    "code": f"empty_{name}_split",
                    "message": f"Not enough Episodes to populate the {name} split.",
                }
            )

    report = {
        "schema_version": SPLIT_SCHEMA,
        "seed": arguments.seed,
        "requested_ratios": ratios,
        "episode_count": len(episodes),
        "frame_count": sum(episode["frame_count"] for episode in episodes),
        "splits": splits,
        "summary": {name: summarize(splits[name]) for name in SPLIT_NAMES},
        "warnings": warnings,
    }
    try:
        temporary.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        temporary.replace(output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise

    print(f"episode_count={len(episodes)}")
    for name in SPLIT_NAMES:
        print(f"{name}_episodes={len(splits[name])}")
    print(f"output={output}")
    print("DATASET_SPLIT_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())