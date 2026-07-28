import os

from .providers.mock import MockPolicyProvider


def create_provider():
    provider_name = os.environ.get("POLICY_PROVIDER", "mock").strip().lower()
    if provider_name == "mock":
        return MockPolicyProvider()
    raise ValueError(f"unknown policy provider: {provider_name}")
