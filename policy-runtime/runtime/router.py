import os

from .providers.mock import MockPolicyProvider


def create_provider():
    provider_name = os.environ.get("POLICY_PROVIDER", "mock").strip().lower()
    if provider_name == "mock":
        return MockPolicyProvider()
    if provider_name == "smolvla":
        from .providers.smolvla import SmolVLAPolicyProvider

        return SmolVLAPolicyProvider()
    raise ValueError(f"unknown policy provider: {provider_name}")
