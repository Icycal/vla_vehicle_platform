# SmolVLA Model Lifecycle

Chitu provides a controlled model lifecycle for Shadow evaluation. The browser does not upload model archives through the ROS API because model artifacts are commonly larger than 1 GB. Instead, the device downloads a selected Hugging Face repository and revision through a whitelisted background Job.

## Install

Open **Engineering Tools → Model Upgrade and Versions** and provide:

- a local version name, such as `ackermann-corridor-v2`;
- a Hugging Face repository id, such as `organization/model-name`;
- an immutable revision or commit hash when possible;
- the LeRobot dataset path or dataset version used for training.

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

Activation updates `run/config/smolvla-runtime.env`, restarts the SmolVLA Runtime, and waits for its health check. If startup fails, the script restores the previous environment and attempts to restart the previous model. The Runtime remains Shadow-only and does not gain permission to publish final chassis commands.

## External Training Output

If a fine-tuned model is not hosted on Hugging Face, copy it into `run/models/providers/smolvla/<version>` with `scp` or the release pipeline. It must contain at least:

```text
config.json
model.safetensors
policy_preprocessor.json
policy_postprocessor.json
```

Add a `vehicle_model_manifest.json` following schema `vehicle.policy-model.v1`; the model then appears in the web catalog and can be activated safely.
The model API is provider-neutral. `smolvla` is the first registered provider adapter; future providers implement the same install, manifest, validation, activation, and rollback contract without changing the platform core.
