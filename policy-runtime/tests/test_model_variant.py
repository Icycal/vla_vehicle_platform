import importlib.util
import tempfile
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[2] / "tools/model/prepare_model_variant.py"
SPEC = importlib.util.spec_from_file_location("prepare_model_variant", TOOL_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ModelVariantTest(unittest.TestCase):
    def test_manifest_is_copied_instead_of_hard_linked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "vehicle_model_manifest.json"
            target = root / "target.json"
            source.write_text('{"precision":"mixed"}\n', encoding="utf-8")
            MODULE.copy_or_link(str(source), str(target))
            target.write_text('{"precision":"int8"}\n', encoding="utf-8")
            self.assertEqual(source.read_text(encoding="utf-8"), '{"precision":"mixed"}\n')


if __name__ == "__main__":
    unittest.main()
