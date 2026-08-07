#!/usr/bin/env python3

import torch


def patch_jetson_distributed() -> None:
    distributed = getattr(torch, "distributed", None)
    if distributed is None:
        return
    if not hasattr(distributed, "is_initialized"):
        distributed.is_initialized = lambda: False
    if not hasattr(distributed, "destroy_process_group"):
        distributed.destroy_process_group = lambda *args, **kwargs: None


patch_jetson_distributed()

from lerobot.scripts.lerobot_train import main


if __name__ == "__main__":
    raise SystemExit(main())