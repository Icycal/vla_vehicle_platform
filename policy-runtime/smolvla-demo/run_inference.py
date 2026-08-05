#!/usr/bin/env python3

import argparse
import json
import time
from pathlib import Path

import cv2
import numpy as np
import torch

from lerobot.configs.policies import PreTrainedConfig
from lerobot.policies.factory import make_pre_post_processors
from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy
from lerobot.utils.control_utils import prepare_observation_for_inference


MODEL_DIR = Path('/models/smolvla_base')
VLM_MANIFEST = Path(
    '/models/huggingface/smolvlm-manifest.json'
)


def parse_arguments():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        '--camera1',
        type=Path,
    )
    parser.add_argument(
        '--camera2',
        type=Path,
    )
    parser.add_argument(
        '--camera3',
        type=Path,
    )

    parser.add_argument(
        '--task',
        default='move forward safely',
    )

    parser.add_argument(
        '--state',
        type=float,
        nargs=6,
        default=[0, 0, 0, 0, 0, 0],
    )

    parser.add_argument(
        '--print-actions',
        type=int,
        default=5,
    )

    return parser.parse_args()


def create_synthetic_image(camera_index):
    image = np.zeros(
        (256, 256, 3),
        dtype=np.uint8,
    )

    if camera_index == 1:
        image[:, :, 0] = 80
    elif camera_index == 2:
        image[:, :, 1] = 80
    else:
        image[:, :, 2] = 80

    cv2.rectangle(
        image,
        (80, 80),
        (176, 176),
        (255, 255, 255),
        thickness=-1,
    )

    cv2.putText(
        image,
        f'CAMERA {camera_index}',
        (35, 230),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (255, 255, 0),
        2,
    )

    return image


def load_rgb_image(path, camera_index):
    if path is None:
        print(
            f'camera{camera_index}: '
            'using generated synthetic image'
        )
        return create_synthetic_image(
            camera_index
        )

    image_bgr = cv2.imread(
        str(path),
        cv2.IMREAD_COLOR,
    )

    if image_bgr is None:
        raise FileNotFoundError(
            f'Unable to read image: {path}'
        )

    image_rgb = cv2.cvtColor(
        image_bgr,
        cv2.COLOR_BGR2RGB,
    )

    image_rgb = cv2.resize(
        image_rgb,
        (256, 256),
        interpolation=cv2.INTER_AREA,
    )

    print(
        f'camera{camera_index}: '
        f'loaded {path}'
    )

    return np.ascontiguousarray(
        image_rgb
    )


def find_vlm_snapshot():
    dependency = json.loads(
        VLM_MANIFEST.read_text(
            encoding='utf-8'
        )
    )

    vlm_path = Path(
        dependency['snapshot_path']
    )

    if not vlm_path.is_dir():
        raise FileNotFoundError(
            f'VLM snapshot not found: '
            f'{vlm_path}'
        )

    return vlm_path, dependency


def print_model_input(model_input):
    print()
    print('Preprocessed model input:')

    for key, value in model_input.items():
        if isinstance(value, torch.Tensor):
            print(
                f'  {key}: '
                f'shape={tuple(value.shape)}, '
                f'dtype={value.dtype}, '
                f'device={value.device}'
            )
        elif key == 'task':
            print(
                f'  task: {value}'
            )


def postprocess_action(
    postprocessor,
    normalized_action,
):
    action = postprocessor(
        normalized_action
    )

    return (
        action
        .squeeze(0)
        .detach()
        .cpu()
        .numpy()
    )


def main():
    arguments = parse_arguments()

    if not torch.cuda.is_available():
        raise RuntimeError(
            'CUDA is not available'
        )

    vlm_path, dependency = (
        find_vlm_snapshot()
    )

    print('SmolVLA model:', MODEL_DIR)
    print(
        'SmolVLM2 revision:',
        dependency['resolved_revision'],
    )
    print(
        'CUDA device:',
        torch.cuda.get_device_name(),
    )
    print('Task:', arguments.task)
    print('State:', arguments.state)

    config = (
        PreTrainedConfig.from_pretrained(
            MODEL_DIR,
            local_files_only=True,
        )
    )

    config.device = 'cuda'
    config.vlm_model_name = str(
        vlm_path
    )

    torch.cuda.empty_cache()
    torch.cuda.reset_peak_memory_stats()

    load_started = time.perf_counter()

    policy = (
        SmolVLAPolicy.from_pretrained(
            MODEL_DIR,
            config=config,
            local_files_only=True,
            strict=True,
        )
    )

    policy.eval()
    policy.reset()

    torch.cuda.synchronize()

    load_seconds = (
        time.perf_counter()
        - load_started
    )

    print()
    print(
        'Model load seconds:',
        round(load_seconds, 3),
    )

    preprocessor, postprocessor = (
        make_pre_post_processors(
            policy_cfg=config,
            pretrained_path=str(
                MODEL_DIR
            ),
            preprocessor_overrides={
                'tokenizer_processor': {
                    'tokenizer_name': str(
                        vlm_path
                    ),
                },
                'device_processor': {
                    'device': 'cuda',
                },
            },
        )
    )

    raw_observation = {
        'observation.state': np.array(
            arguments.state,
            dtype=np.float32,
        ),
        'observation.images.camera1': (
            load_rgb_image(
                arguments.camera1,
                1,
            )
        ),
        'observation.images.camera2': (
            load_rgb_image(
                arguments.camera2,
                2,
            )
        ),
        'observation.images.camera3': (
            load_rgb_image(
                arguments.camera3,
                3,
            )
        ),
    }

    model_input = (
        prepare_observation_for_inference(
            raw_observation,
            device=torch.device('cuda'),
            task=arguments.task,
        )
    )

    model_input = preprocessor(
        model_input
    )

    print_model_input(
        model_input
    )

    torch.cuda.synchronize()
    inference_started = (
        time.perf_counter()
    )

    with torch.inference_mode():
        action_chunk = (
            policy.predict_action_chunk(
                model_input
            )
        )

    torch.cuda.synchronize()

    inference_seconds = (
        time.perf_counter()
        - inference_started
    )

    print()
    print(
        'Normalized action chunk shape:',
        tuple(action_chunk.shape),
    )

    action_count = min(
        arguments.print_actions,
        action_chunk.shape[1],
    )

    print()
    print(
        f'First {action_count} '
        'postprocessed actions:'
    )

    for action_index in range(
        action_count
    ):
        normalized_action = (
            action_chunk[
                :,
                action_index,
            ]
        )

        action = postprocess_action(
            postprocessor,
            normalized_action,
        )

        print(
            f'  action[{action_index}]:',
            action.tolist(),
        )

    print()
    print(
        'Inference seconds:',
        round(
            inference_seconds,
            3,
        ),
    )

    print(
        'CUDA allocated MB:',
        round(
            torch.cuda.memory_allocated()
            / 1024**2,
            2,
        ),
    )

    print(
        'CUDA peak MB:',
        round(
            torch.cuda.max_memory_allocated()
            / 1024**2,
            2,
        ),
    )

    print()
    print(
        'SMOLVLA_DEMO_INFERENCE_PASS'
    )


if __name__ == '__main__':
    main()