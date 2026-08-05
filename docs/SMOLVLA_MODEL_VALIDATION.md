# SmolVLA Model Validation

This stage validates model artifacts only. It does not start
`wheeltec_robot_node`, publish `/cmd_vel`, or connect inference to ROS 2.

## Locked Artifacts

- SmolVLA: `lerobot/smolvla_base`
- SmolVLA revision: `c83c3163b8ca9b7e67c509fffd9121e66cb96205`
- VLM: `HuggingFaceTB/SmolVLM2-500M-Video-Instruct`
- VLM revision: `7b375e1b73b11138ff12fe22c8f2822d8fe03467`

SmolVLA requires the VLM repository during construction. The VLM snapshot is
stored separately and its absolute cache snapshot is injected into the policy
configuration for deterministic offline loading.

## Download

Use a reachable Hugging Face endpoint and the locked revisions:

```bash
HF_ENDPOINT=https://hf-mirror.com \
SMOLVLA_MODEL_REVISION=c83c3163b8ca9b7e67c509fffd9121e66cb96205 \
./scripts/download_smolvla_model.sh

HF_ENDPOINT=https://hf-mirror.com \
SMOLVLM_REVISION=7b375e1b73b11138ff12fe22c8f2822d8fe03467 \
./scripts/download_smolvlm_dependency.sh
```

Model data is stored under `run/models` and is excluded from Git.

## Offline Verification

```bash
./scripts/verify_smolvla_model_offline.sh
```

The verification container uses `--network none`, mounts both artifact roots
read-only, and enables the Hugging Face and Transformers offline modes.

Validated on the Jetson Orin NX:

- SmolVLA parameters: `450046176`
- Offline load time: `10.64` seconds
- CUDA allocated after load: `872.07` MiB
- CUDA peak during load: `1549.18` MiB
- Result: `SMOLVLA_OFFLINE_LOAD_PASS`

The base feature contract contains one six-element state vector, three
`256x256` RGB observations, and one six-element action vector. This is a
pretraining contract, not the Ackermann vehicle control contract; an adapter
must map vehicle observations and policy outputs before Shadow integration.

## Offline Image Inference Demo

The reproducible image Demo is split between versioned source and ignored runtime data:

- `policy-runtime/smolvla-demo/run_inference.py`: tracked inference entry point.
- `policy-runtime/compose.smolvla-demo.yaml`: tracked container definition.
- `config/smolvla-demo.env.example`: tracked configuration template.
- `run/config/smolvla-demo.env`: local runtime configuration.
- `run/test/smolvla-demo/images`: local camera frames.
- `run/test/smolvla-demo/logs/image-inference.log`: local inference output.

Run it with:

```bash
cp config/smolvla-demo.env.example run/config/smolvla-demo.env
./scripts/run_smolvla_demo.sh
```

The container runs with no network, mounts model artifacts and Demo inputs read-only, then exits
and is removed. The validated Orin NX run loaded one real USB camera frame into all three required
image inputs and produced an action tensor with shape `(1, 50, 6)` in `2.130` seconds. This test
proves offline image-to-action inference only; it does not define vehicle action semantics or grant
the model control authority.
