from typing import Any, Dict

from .model_manifest import InferenceSpec


def _matches_prefix(name: str, prefixes) -> bool:
    return any(name == prefix or name.startswith(prefix + ".") for prefix in prefixes)


def apply_runtime_quantization(model, inference: InferenceSpec) -> Dict[str, Any]:
    if inference.weight_precision != "int8":
        return {
            "applied": False,
            "engine": "none",
            "scheme": "none",
            "weight_precision": inference.weight_precision,
            "activation_precision": inference.activation_precision,
            "module_count": 0,
        }

    import torch

    quantization = inference.quantization
    torchao_quantize = None
    torchao_config = None
    if quantization.engine == "torchao":
        try:
            from torchao.quantization import Int8WeightOnlyConfig, quantize_
        except ImportError as error:
            raise RuntimeError(
                "This INT8 model requires TorchAO, but torchao is not installed in the runtime image"
            ) from error
        torchao_quantize = quantize_
        torchao_config = Int8WeightOnlyConfig
    include_modules = quantization.include_modules or ("model.vlm_with_expert.vlm",)
    exclude_modules = quantization.exclude_modules
    targets = []
    for name, module in model.named_modules():
        if not isinstance(module, torch.nn.Linear):
            continue
        if not _matches_prefix(name, include_modules):
            continue
        if _matches_prefix(name, exclude_modules):
            continue
        targets.append(name)
    if not targets:
        raise RuntimeError(
            "INT8 quantization selected no Linear modules; check include_modules and exclude_modules"
        )
    if quantization.engine == "torchao":
        target_names = frozenset(targets)

        def filter_fn(module, fqn):
            return fqn in target_names

        torchao_quantize(model, torchao_config(), filter_fn=filter_fn)
    elif quantization.engine == "pytorch-native":
        class Int8WeightOnlyLinear(torch.nn.Module):
            def __init__(self, linear):
                super().__init__()
                weight = linear.weight.detach().float()
                scale = weight.abs().amax(dim=1).clamp_min(1e-8) / 127.0
                quantized = torch.round(weight / scale.unsqueeze(1)).clamp(-127, 127).to(torch.int8)
                self.register_buffer("qweight", quantized)
                self.register_buffer("scale", scale.to(linear.weight.dtype))
                if linear.bias is None:
                    self.bias = None
                else:
                    self.register_buffer("bias", linear.bias.detach())
                self.weight = type(
                    "WeightMetadata",
                    (),
                    {
                        "dtype": linear.weight.dtype,
                        "device": linear.weight.device,
                        "shape": linear.weight.shape,
                    },
                )()
                self.functional = torch.nn.functional

            def forward(self, inputs):
                weight = self.qweight.to(inputs.dtype) * self.scale.to(inputs.dtype).unsqueeze(1)
                bias = self.bias.to(inputs.dtype) if self.bias is not None else None
                return self.functional.linear(inputs, weight, bias)

        modules = dict(model.named_modules())
        for name in targets:
            parent_name, _, child_name = name.rpartition(".")
            parent = modules[parent_name] if parent_name else model
            setattr(parent, child_name, Int8WeightOnlyLinear(modules[name]))
    else:
        raise RuntimeError(f"Unsupported INT8 quantization engine: {quantization.engine}")
    return {
        "applied": True,
        "engine": quantization.engine,
        "scheme": quantization.scheme,
        "weight_precision": "int8",
        "activation_precision": quantization.activation_dtype,
        "module_count": len(targets),
        "include_modules": list(include_modules),
        "exclude_modules": list(exclude_modules),
    }
