# SmolVLA Model Lifecycle

Chitu provides a controlled model lifecycle for Shadow evaluation. Models may be downloaded from a
trusted Hugging Face repository or imported from a directory already available on the vehicle. The
browser does not proxy multi-gigabyte model payloads through the ROS API.

## Install

Open **Engineering Tools → Model Upgrade and Versions** and provide:

- a local version name, such as `ackermann-corridor-v2`;
- a Hugging Face repository id, such as `organization/model-name`;
- an immutable revision or commit hash when possible;
- the LeRobot dataset path or dataset version used for training.

For a locally trained model, select **Vehicle local directory** and enter an absolute path or a path
relative to the project root. The import Job copies or hard-links the model into the managed model
registry; it never activates the imported model automatically.

The install Job writes the model to `run/models/providers/smolvla/<version>`, verifies the required SmolVLA files, and creates `vehicle_model_manifest.json`. Installation does not change the running Runtime.

For a private repository, create the ignored file `run/config/model-download.env`:

```bash
HF_TOKEN=hf_xxx
# Optional mirror:
# HF_ENDPOINT=https://huggingface.co
```

Never commit this file.

## Activate and Roll Back

Activation is available only when:

- the vehicle is in `VLA_SHADOW`;
- no Active Debug session is running or paused;
- no Episode is recording;
- the recent vehicle command is stationary.

The web catalog reads the model `action.schema`, compares it with every Mobility Plugin `accepts`
entry, and displays the current plugin, recommended plugin, output fields, and compatibility state.
Plugin selection follows the model Action Schema, not the physical vehicle name. For example, an
Ackermann vehicle trained from `cmd_vel.linear.x` and `cmd_vel.angular.z` still uses
`chitu.mobility.twist`. `chitu.mobility.ackermann` is only valid for models whose schema is
`chitu.action.ackermann.v1` with `speed_mps` and `steering_angle_rad` fields.

An incompatible model is disabled in the browser and rejected again by the API and activation
script. A model without an Action Descriptor is marked as unknown and remains on the safe Zero
Adapter path.

Activation updates `run/config/smolvla-runtime.env`, restarts the SmolVLA Runtime, and waits for its health check. If startup fails, the script restores the previous environment and attempts to restart the previous model. The Runtime remains Shadow-only and does not gain permission to publish final chassis commands.

## External Training Output

If a fine-tuned model is not hosted on Hugging Face, copy it into `run/models/providers/smolvla/<version>` with `scp` or the release pipeline. It must contain at least:

```text
config.json
model.safetensors
policy_preprocessor.json
policy_postprocessor.json
```

The import flow creates or upgrades `vehicle_model_manifest.json` to
`chitu.policy-model.v2`; the model then appears in the web catalog and can be activated safely.
The model API is provider-neutral. `smolvla` is the first registered provider adapter; future providers implement the same install, manifest, validation, activation, and rollback contract without changing the platform core.

## Runtime Backend and Precision

Model identity, execution backend, numerical precision, hardware compatibility, and Action Schema
are separate contracts. A representative runtime descriptor is:

```json
{
  "schema_version": "chitu.policy-model.v2",
  "provider": "smolvla",
  "inference": {
    "backend": "pytorch",
    "artifact_format": "safetensors",
    "weight_precision": "int8",
    "activation_precision": "bfloat16",
    "quantization": {
      "engine": "pytorch-native",
      "scheme": "weight_only",
      "include_modules": ["model.vlm_with_expert.vlm"]
    }
  },
  "compatibility": {
    "platforms": ["linux"],
    "architectures": [],
    "accelerators": []
  }
}
```

An empty platform, architecture, or accelerator list means that the model package does not impose
that restriction. Backend activation still checks required runtime libraries and accelerator
support. The standard vehicle-side INT8 variant command explicitly targets Linux without fixing a
CPU architecture, username, installation directory, or NVIDIA GPU model.
Legacy manifests remain valid and default to the existing PyTorch mixed BF16/FP32 path.

### Prepare an INT8 Variant

The web model card offers **Generate INT8**. The equivalent command is:

```bash
./scripts/prepare_policy_model_variant.sh \
  smolvla corridor-v2 corridor-v2-int8 int8
```

The first INT8 implementation is W8A16 runtime quantization. The built-in `pytorch-native` reference
engine converts selected VLM Linear weights to per-output-channel INT8 after the trained checkpoint
is loaded, while activations remain BF16 and the Flow-Matching expert, state projection, Action
projection, postprocessor, Policy Action protocol, Mobility Adapter, and chassis command remain
floating point. Quantization therefore does not alter the model's Action Schema. The reference
engine prioritizes portability and correctness; it dequantizes each selected weight for the Linear
operation, so lower resident parameter storage does not guarantee lower latency.

TorchAO remains an optional acceleration engine for platform/PyTorch combinations that support it.
Set `inference.quantization.engine` to `torchao` only after validating that runtime combination.

Build an INT8-capable runtime image without replacing the NVIDIA PyTorch packages:

```bash
SMOLVLA_INSTALL_TORCHAO=1 \
SMOLVLA_TORCHAO_VERSION=<validated-version> \
./scripts/build_smolvla_runtime.sh
```

Activation of a TorchAO INT8 variant checks that TorchAO imports successfully in the selected
runtime image. A missing or incompatible dependency rejects activation before the active model
configuration changes. The current Jetson NVIDIA PyTorch image omits distributed components that
recent TorchAO releases import, so vehicle-generated variants default to `pytorch-native` rather
than silently selecting an incompatible accelerator library.

### Compare Floating and Quantized Outputs

Save the `actions` arrays from equivalent BF16 and INT8 single-step debug runs and compare them:

```bash
python3 tools/model/compare_action_outputs.py \
  --baseline run/test/bf16-actions.json \
  --candidate run/test/int8-actions.json \
  --max-mae 0.02 \
  --max-error 0.08
```

The report includes MAE, RMSE, maximum absolute error, per-dimension MAE, and sign changes. Passing
this offline comparison does not replace Replay, Shadow, low-speed supervised vehicle testing, or
Safety Guard acceptance.
