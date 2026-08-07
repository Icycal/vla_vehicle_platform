# SmolVLA Training

## Scope

The training workspace has two profiles that share the same LeRobot Dataset, policy feature
contract, local base checkpoint, VLM snapshot, and training run manifest:

- `training/profiles/orin-smoke.json`: five backward/update steps on Jetson Orin NX. This is a
  compatibility and resource test only; it does not save a deployable checkpoint.
- `training/profiles/gpu-finetune.json`: the starting profile for full fine-tuning on an x86_64
  CUDA workstation. It saves periodic checkpoints and enables image augmentation.

Neither profile changes the ROS control boundary. Training runs completely outside ROS and must not
be performed while the vehicle is moving or chassis control nodes are active.

## Orin Smoke

Prepare a local LeRobot Dataset, then run:

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/run_smolvla_training.sh \
  training/profiles/orin-smoke.json \
  datasets/lerobot/smolvla-training-smoke \
  run/training/orin-smoke-001
```

The runner:

1. reads `vehicle_conversion_manifest.json` and derives the actual image, state, and action shapes;
2. creates a temporary policy overlay without copying the 873MB base checkpoint;
3. resolves the exact local SmolVLM snapshot from `smolvlm-manifest.json`;
4. runs LeRobot fully offline in the pinned compatibility image;
5. applies a Jetson-only-safe `torch.distributed` compatibility shim;
6. records the training log, tegrastats log, resolved overlay, hashes, and final metrics.

The runner refuses to overwrite an existing output directory. Runtime artifacts are stored under
`run/training` and remain excluded from Git.

## Full GPU Fine-tuning

Build the x86_64 CUDA image on the training workstation:

```bash
./scripts/build_smolvla_training_image.sh
```

Synchronize these runtime inputs to the workstation without committing them to Git:

```text
datasets/lerobot/<dataset>/
run/models/smolvla_base/
run/models/huggingface/smolvlm-manifest.json
run/models/huggingface/hub/models--HuggingFaceTB--SmolVLM2-500M-Video-Instruct/
```

Run the full profile:

```bash
./scripts/run_smolvla_training.sh \
  training/profiles/gpu-finetune.json \
  datasets/lerobot/ackermann-training-v1 \
  run/training/ackermann-smolvla-v1
```

Useful temporary overrides include:

```bash
TRAINING_STEPS=100 \
TRAINING_BATCH_SIZE=2 \
TRAINING_WANDB_ENABLE=false \
./scripts/run_smolvla_training.sh <profile> <dataset> <output>
```

The x86 image and full profile are scaffolding until they are built and benchmarked on the selected
GPU server. Batch size, worker count, checkpoint frequency, and augmentation must be tuned there.

## Artifact Boundary

Each run produces `training_run_manifest.json` plus the exact generated policy/preprocessor overlay.
Full training checkpoints remain native LeRobot checkpoints. A later promotion step will validate a
chosen checkpoint, calculate model checksums, add offline evaluation results, and package it under
`artifacts/models/<model-id>` for deployment to Orin.

The existing `container-shadow-001` data contains zero executed actions. It is suitable only for
format, CUDA, backward-pass, and resource validation; it must not be treated as training evidence.