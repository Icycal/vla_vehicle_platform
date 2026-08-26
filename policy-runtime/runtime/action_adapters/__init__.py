import os

from .manifest import ManifestActionAdapter
from .shadow_twist import ShadowTwistActionAdapter


def create_action_adapter(model_dir):
    mode = os.environ.get("SMOLVLA_ACTION_ADAPTER", "zero").strip().lower()
    if mode == "manifest":
        return ManifestActionAdapter(
            model_dir=model_dir,
            horizon=int(os.environ.get("SMOLVLA_ACTION_HORIZON", "8")),
        )
    return ShadowTwistActionAdapter.from_environment()
