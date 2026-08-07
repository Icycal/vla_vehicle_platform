#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Write a reproducible SmolVLA training run manifest.")
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--base-model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--resource-log", type=Path)
    parser.add_argument("--status", required=True, type=int)
    parser.add_argument("--git-commit", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--started-at", required=True)
    return parser.parse_args()


def sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_last_metrics(log_text: str) -> dict:
    matches = re.findall(
        r"step:(\d+).*?loss:([0-9.eE+-]+).*?grdn:([0-9.eE+-]+).*?lr:([0-9.eE+-]+).*?updt_s:([0-9.eE+-]+).*?data_s:([0-9.eE+-]+)",
        log_text,
    )
    if not matches:
        return {}
    step, loss, gradient_norm, learning_rate, update_seconds, data_seconds = matches[-1]
    return {
        "step": int(step),
        "loss": float(loss),
        "gradient_norm": float(gradient_norm),
        "learning_rate": float(learning_rate),
        "update_seconds": float(update_seconds),
        "data_seconds": float(data_seconds),
    }


def parse_tegrastats(path: Path | None) -> dict:
    if path is None or not path.is_file():
        return {}
    peaks = {
        "ram_used_mb": 0,
        "swap_used_mb": 0,
        "gpu_utilization_percent": 0,
        "cpu_temperature_c": 0.0,
        "gpu_temperature_c": 0.0,
        "junction_temperature_c": 0.0,
        "input_power_mw": 0,
    }
    samples = 0
    for line in path.read_text(errors="replace").splitlines():
        samples += 1
        patterns = {
            "ram_used_mb": r"RAM (\d+)/",
            "swap_used_mb": r"SWAP (\d+)/",
            "gpu_utilization_percent": r"GR3D_FREQ (\d+)%",
            "cpu_temperature_c": r"cpu@([0-9.]+)C",
            "gpu_temperature_c": r"gpu@([0-9.]+)C",
            "junction_temperature_c": r"tj@([0-9.]+)C",
            "input_power_mw": r"VDD_IN (\d+)mW",
        }
        for key, pattern in patterns.items():
            match = re.search(pattern, line)
            if match:
                peaks[key] = max(peaks[key], float(match.group(1)))
    peaks["ram_used_mb"] = int(peaks["ram_used_mb"])
    peaks["swap_used_mb"] = int(peaks["swap_used_mb"])
    peaks["gpu_utilization_percent"] = int(peaks["gpu_utilization_percent"])
    peaks["input_power_mw"] = int(peaks["input_power_mw"])
    return {"samples": samples, "peaks": peaks}


def main() -> int:
    arguments = parse_arguments()
    runtime = json.loads(arguments.runtime.read_text())
    dataset_manifest = arguments.dataset / "vehicle_conversion_manifest.json"
    base_manifest = arguments.base_model / "model-manifest.json"
    log_text = arguments.log.read_text(errors="replace") if arguments.log.exists() else ""
    arguments.output.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema_version": "vehicle.training.run.v1",
        "status": "passed" if arguments.status == 0 else "failed",
        "exit_code": arguments.status,
        "started_at": arguments.started_at,
        "finished_at": datetime.now(timezone.utc).isoformat(),
        "git_commit": arguments.git_commit,
        "image": arguments.image,
        "profile": runtime["profile"],
        "dataset": {
            "repo_id": runtime["dataset_repo_id"],
            "episode_count": runtime["dataset_episode_count"],
            "frame_count": runtime["dataset_frame_count"],
            "manifest_sha256": sha256(dataset_manifest),
        },
        "base_model_manifest_sha256": sha256(base_manifest),
        "vlm_revision": runtime["vlm_revision"],
        "policy_input_features": runtime["policy_input_features"],
        "policy_output_features": runtime["policy_output_features"],
        "last_metrics": parse_last_metrics(log_text),
        "resources": parse_tegrastats(arguments.resource_log),
        "log_sha256": sha256(arguments.log),
        "resource_log_sha256": sha256(arguments.resource_log) if arguments.resource_log else None,
    }
    (arguments.output / "training_run_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n"
    )
    print(arguments.output / "training_run_manifest.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())