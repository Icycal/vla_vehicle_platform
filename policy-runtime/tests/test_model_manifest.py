import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from runtime.model_manifest import load_model_runtime_manifest


class ModelManifestTest(unittest.TestCase):
    def test_legacy_model_defaults_to_pytorch_mixed_precision(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = load_model_runtime_manifest(Path(directory), "smolvla")
        self.assertTrue(manifest.legacy)
        self.assertEqual(manifest.inference.backend, "pytorch")
        self.assertEqual(manifest.inference.weight_precision, "mixed")

    def test_int8_model_uses_native_weight_only_by_default(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "vehicle_model_manifest.json").write_text(
                json.dumps(
                    {
                        "schema_version": "chitu.policy-model.v2",
                        "provider": "smolvla",
                        "inference": {
                            "backend": "pytorch",
                            "weight_precision": "int8",
                            "activation_precision": "bfloat16",
                            "quantization": {
                                "engine": "pytorch-native",
                                "scheme": "weight_only",
                                "include_modules": ["model.vlm_with_expert.vlm"],
                            },
                        },
                    }
                ),
                encoding="utf-8",
            )
            manifest = load_model_runtime_manifest(path, "smolvla")
        self.assertFalse(manifest.legacy)
        self.assertEqual(manifest.inference.weight_precision, "int8")
        self.assertEqual(manifest.inference.quantization.engine, "pytorch-native")

    def test_provider_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "vehicle_model_manifest.json").write_text(
                json.dumps({"provider": "other"}), encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "does not match"):
                load_model_runtime_manifest(path, "smolvla")

    def test_unsupported_backend_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "vehicle_model_manifest.json").write_text(
                json.dumps({"provider": "smolvla", "inference": {"backend": "unknown"}}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "Unsupported inference backend"):
                load_model_runtime_manifest(path, "smolvla")


if __name__ == "__main__":
    unittest.main()
