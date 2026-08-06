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


## Persistent Provider

The next validation layer is implemented by `runtime.providers.smolvla`. Unlike the one-shot Demo,
it keeps the model resident, exposes the project Protobuf interface, rejects concurrent inference,
and reports health while inference is running. The default adapter deliberately returns zero Twist
candidates because the base checkpoint's six action dimensions are not vehicle controls.

Run:

```bash
cp config/smolvla-runtime.env.example run/config/smolvla-runtime.env
./scripts/build_smolvla_runtime.sh
./scripts/run_smolvla_runtime.sh
./scripts/test_smolvla_runtime.sh
```

Passing this smoke test proves persistent image-to-action inference and protocol correlation. It
does not prove useful Ackermann action quality; that requires the Dataset Exporter, vehicle-specific
training, Rosbag Replay, and real Shadow evaluation.


## Persistent Runtime Validation - 2026-08-06

The persistent runtime passed on the Orin NX with the real model snapshot and the captured USB
camera frame:

- Provider: `smolvla-runtime`.
- Model: `smolvla-base-c83c3163`.
- Default adapter: `smolvla-shadow-zero-v1`.
- Warm direct inference: approximately `0.924` seconds; round trip approximately `0.991` seconds.
- Repeated ROS Shadow inference: approximately `0.916` to `1.050` seconds.
- Health response during active inference: approximately `197` milliseconds.
- A second overlapping prediction was rejected with `SmolVLA provider is busy`.
- The response contained eight finite zero Twist candidates correlated to the source Observation.
- The C++ Gateway continued receiving camera Observations while inference was active.
- Final `/cmd_vel` remained exactly zero and `wheeltec_robot_node` was not started.

This closes the persistent-provider infrastructure item, but not the Phase 1 model-quality exit
condition. The base checkpoint remains semantically incompatible with Ackermann control until a
vehicle Dataset Exporter, vehicle-specific training, Replay validation, and a trained action adapter
are complete.
