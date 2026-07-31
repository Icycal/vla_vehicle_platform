import importlib.metadata
import json
import os
from pathlib import Path

import torch


baseline_path = Path("/opt/vla/nvidia-package-baseline.json")
baseline = json.loads(baseline_path.read_text())

print("=== NVIDIA package comparison ===")

for package_name, expected_version in baseline.items():
    actual_version = importlib.metadata.version(package_name)

    print(
        f"{package_name}: "
        f"expected={expected_version} "
        f"actual={actual_version}"
    )

    if actual_version != expected_version:
        raise RuntimeError(
            f"{package_name} changed from "
            f"{expected_version} to {actual_version}"
        )

expected_lerobot_version = os.environ.get(
    "EXPECTED_LEROBOT_VERSION",
    "0.4.3",
)

actual_lerobot_version = importlib.metadata.version("lerobot")

print("=== LeRobot environment ===")
print("lerobot:", actual_lerobot_version)

if actual_lerobot_version != expected_lerobot_version:
    raise RuntimeError(
        f"Expected LeRobot {expected_lerobot_version}, "
        f"got {actual_lerobot_version}"
    )

for package_name in (
    "transformers",
    "accelerate",
    "num2words",
    "safetensors",
    "numpy",
    "opencv-python-headless",
):
    print(
        package_name,
        importlib.metadata.version(package_name),
    )

from lerobot.policies.smolvla.configuration_smolvla import (
    SmolVLAConfig,
)
from lerobot.policies.smolvla.modeling_smolvla import (
    SmolVLAPolicy,
)

print("SmolVLAConfig:", SmolVLAConfig)
print("SmolVLAPolicy:", SmolVLAPolicy)
print("SMOLVLA_IMPORT_PASS")

require_cuda = os.environ.get("REQUIRE_CUDA", "0") == "1"

print("torch:", torch.__version__)
print("torch_cuda:", torch.version.cuda)
print("cuda_available:", torch.cuda.is_available())

if require_cuda:
    if not torch.cuda.is_available():
        raise RuntimeError(
            "CUDA unavailable after LeRobot installation"
        )

    device = torch.device("cuda:0")

    tensor = torch.arange(
        0,
        1024 * 1024,
        device=device,
        dtype=torch.float32,
    )

    result = tensor.square().mean()

    torch.cuda.synchronize(device)

    print("device:", torch.cuda.get_device_name(device))
    print("cuda_result:", float(result.item()))
    print("LEROBOT_CUDA_PASS")

print("LEROBOT_ENVIRONMENT_PASS")
