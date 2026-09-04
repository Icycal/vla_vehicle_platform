import sys
import types
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from runtime.model_manifest import InferenceSpec, QuantizationSpec
from runtime.quantization import apply_runtime_quantization


class QuantizationTest(unittest.TestCase):
    def test_non_int8_model_is_left_unchanged(self):
        result = apply_runtime_quantization(object(), InferenceSpec())
        self.assertFalse(result["applied"])
        self.assertEqual(result["module_count"], 0)

    def test_missing_torchao_has_actionable_error(self):
        inference = InferenceSpec(
            weight_precision="int8",
            quantization=QuantizationSpec(engine="torchao", scheme="weight_only"),
        )
        torch_module = types.ModuleType("torch")
        torch_module.nn = types.SimpleNamespace(Linear=type("Linear", (), {}))
        with mock.patch.dict(
            sys.modules,
            {"torch": torch_module, "torchao": None, "torchao.quantization": None},
        ):
            with self.assertRaisesRegex(RuntimeError, "requires TorchAO"):
                apply_runtime_quantization(object(), inference)

    def test_selected_linear_modules_are_quantized(self):
        class Linear:
            pass

        class Other:
            pass

        class Model:
            def named_modules(self):
                return (
                    ("model.vlm_with_expert.vlm.layers.0", Linear()),
                    ("model.vlm_with_expert.vlm.layers.1", Other()),
                    ("model.vlm_with_expert.vlm.layers.2", Linear()),
                    ("model.action_out_proj", Linear()),
                )

        calls = []
        quantization_module = types.ModuleType("torchao.quantization")
        quantization_module.Int8WeightOnlyConfig = type("Int8WeightOnlyConfig", (), {})

        def quantize(model, config, filter_fn):
            calls.extend(
                name for name, module in model.named_modules() if filter_fn(module, name)
            )

        quantization_module.quantize_ = quantize
        torchao_module = types.ModuleType("torchao")
        torchao_module.quantization = quantization_module
        torch_module = types.ModuleType("torch")
        torch_module.nn = types.SimpleNamespace(Linear=Linear)
        inference = InferenceSpec(
            weight_precision="int8",
            quantization=QuantizationSpec(engine="torchao", scheme="weight_only"),
        )
        with mock.patch.dict(
            sys.modules,
            {
                "torch": torch_module,
                "torchao": torchao_module,
                "torchao.quantization": quantization_module,
            },
        ):
            result = apply_runtime_quantization(Model(), inference)
        self.assertTrue(result["applied"])
        self.assertEqual(result["module_count"], 2)
        self.assertEqual(len(calls), 2)
        self.assertNotIn("model.action_out_proj", calls)


if __name__ == "__main__":
    unittest.main()
