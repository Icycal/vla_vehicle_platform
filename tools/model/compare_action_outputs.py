#!/usr/bin/env python3

import argparse
import json
import math
from pathlib import Path


def load_actions(path: Path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(value, dict):
        value = value.get("actions", value.get("values", value))
    if not isinstance(value, list) or not value:
        raise ValueError(f"No action vectors in {path}")
    vectors = []
    for item in value:
        vector = item.get("values") if isinstance(item, dict) else item
        if not isinstance(vector, list):
            raise ValueError(f"Invalid action vector in {path}")
        vectors.append([float(component) for component in vector])
    return vectors


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare floating-point and quantized action chunks")
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--max-mae", type=float, default=None)
    parser.add_argument("--max-error", type=float, default=None)
    arguments = parser.parse_args()
    baseline = load_actions(arguments.baseline)
    candidate = load_actions(arguments.candidate)
    if len(baseline) != len(candidate):
        raise ValueError("Action horizons differ")
    if any(len(left) != len(right) for left, right in zip(baseline, candidate)):
        raise ValueError("Action dimensions differ")

    errors = []
    sign_changes = 0
    per_dimension = [[] for _ in baseline[0]]
    for left, right in zip(baseline, candidate):
        for index, (reference, measured) in enumerate(zip(left, right)):
            error = measured - reference
            errors.append(error)
            per_dimension[index].append(error)
            if reference != 0.0 and measured != 0.0 and math.copysign(1.0, reference) != math.copysign(1.0, measured):
                sign_changes += 1
    component_count = len(errors)
    mae = sum(abs(value) for value in errors) / component_count
    rmse = math.sqrt(sum(value * value for value in errors) / component_count)
    maximum = max(abs(value) for value in errors)
    result = {
        "schema_version": "chitu.policy-action-comparison.v1",
        "baseline": str(arguments.baseline.resolve()),
        "candidate": str(arguments.candidate.resolve()),
        "horizon": len(baseline),
        "dimensions": len(baseline[0]),
        "mae": mae,
        "rmse": rmse,
        "max_absolute_error": maximum,
        "sign_change_count": sign_changes,
        "per_dimension_mae": [
            sum(abs(value) for value in dimension) / len(dimension) for dimension in per_dimension
        ],
    }
    print(json.dumps(result, indent=2))
    if arguments.max_mae is not None and mae > arguments.max_mae:
        return 2
    if arguments.max_error is not None and maximum > arguments.max_error:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
