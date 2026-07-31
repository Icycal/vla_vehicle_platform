#!/usr/bin/env python3

import argparse
import json
import time
from pathlib import Path

import torch
from lerobot.configs.policies import PreTrainedConfig
from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--model-dir', type=Path, required=True)
    parser.add_argument('--dependency-manifest', type=Path, required=True)
    parser.add_argument('--device', default='cuda')
    arguments = parser.parse_args()

    model_dir = arguments.model_dir.resolve()
    manifest_path = model_dir / 'model-manifest.json'
    if not model_dir.is_dir():
        raise FileNotFoundError(model_dir)
    if not manifest_path.is_file():
        raise FileNotFoundError(manifest_path)
    if arguments.device.startswith('cuda') and not torch.cuda.is_available():
        raise RuntimeError('CUDA was requested but is not available')

    manifest = json.loads(manifest_path.read_text())
    dependency = json.loads(arguments.dependency_manifest.read_text())
    dependency_path = Path(dependency['snapshot_path'])
    if not dependency_path.is_dir():
        raise FileNotFoundError(dependency_path)
    print('repo_id:', manifest['repo_id'])
    print('resolved_revision:', manifest['resolved_revision'])
    print('vlm_revision:', dependency['resolved_revision'])
    print('device:', arguments.device)
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
        torch.cuda.reset_peak_memory_stats()
    started = time.perf_counter()
    config = PreTrainedConfig.from_pretrained(
        model_dir,
        local_files_only=True,
    )
    config.vlm_model_name = str(dependency_path)
    policy = SmolVLAPolicy.from_pretrained(
        model_dir,
        config=config,
        local_files_only=True,
        strict=True,
    )
    policy.eval()
    policy.to(arguments.device)
    if torch.cuda.is_available():
        torch.cuda.synchronize()
    load_seconds = time.perf_counter() - started
    parameter_count = sum(item.numel() for item in policy.parameters())
    print('load_seconds:', round(load_seconds, 3))
    print('parameter_count:', parameter_count)
    print('input_features:', str(policy.config.input_features))
    print('output_features:', str(policy.config.output_features))
    if torch.cuda.is_available():
        print('cuda_device:', torch.cuda.get_device_name())
        print('cuda_memory_mb:', round(torch.cuda.memory_allocated() / 1024**2, 2))
        print('cuda_peak_mb:', round(torch.cuda.max_memory_allocated() / 1024**2, 2))
    print('SMOLVLA_OFFLINE_LOAD_PASS')


if __name__ == '__main__':
    main()
