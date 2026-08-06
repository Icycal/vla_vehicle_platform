from .shadow_twist import ShadowTwistActionAdapter


def create_action_adapter():
    return ShadowTwistActionAdapter.from_environment()
