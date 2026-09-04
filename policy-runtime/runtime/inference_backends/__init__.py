from .pytorch import PyTorchSmolVLABackend


def create_smolvla_backend(model_dir, vlm_manifest, runtime_manifest):
    backend = runtime_manifest.inference.backend
    if backend == "pytorch":
        return PyTorchSmolVLABackend(model_dir, vlm_manifest, runtime_manifest)
    raise ValueError(f"Unsupported SmolVLA inference backend: {backend}")


__all__ = ["create_smolvla_backend", "PyTorchSmolVLABackend"]
