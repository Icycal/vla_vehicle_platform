import os
import platform
import time

import torch


duration_seconds = int(os.environ.get("SMOKE_SECONDS", "15"))

print("platform:", platform.machine())
print("python_torch:", torch.__version__)
print("torch_cuda:", torch.version.cuda)
print("cudnn:", torch.backends.cudnn.version())
print("cuda_available:", torch.cuda.is_available())
print("device_count:", torch.cuda.device_count())

if not torch.cuda.is_available():
    raise RuntimeError("CUDA is not available")

device = torch.device("cuda:0")
properties = torch.cuda.get_device_properties(device)

print("device_name:", properties.name)
print("device_capability:", torch.cuda.get_device_capability(device))
print("device_memory_gb:", round(properties.total_memory / 1024**3, 2))

torch.manual_seed(7)
torch.cuda.manual_seed_all(7)
torch.cuda.reset_peak_memory_stats(device)

matrix_a = torch.randn(
    (2048, 2048),
    device=device,
    dtype=torch.float16,
)
matrix_b = torch.randn(
    (2048, 2048),
    device=device,
    dtype=torch.float16,
)

for _ in range(3):
    matrix_result = matrix_a @ matrix_b

torch.cuda.synchronize(device)

started_at = time.monotonic()
iterations = 0

while time.monotonic() - started_at < duration_seconds:
    matrix_result = matrix_a @ matrix_b
    iterations += 1

torch.cuda.synchronize(device)
elapsed_seconds = time.monotonic() - started_at

convolution = torch.nn.Conv2d(
    in_channels=3,
    out_channels=32,
    kernel_size=3,
    padding=1,
).to(device=device, dtype=torch.float16)

image = torch.randn(
    (1, 3, 512, 512),
    device=device,
    dtype=torch.float16,
)

convolution_result = convolution(image)
torch.cuda.synchronize(device)

matrix_finite = bool(torch.isfinite(matrix_result).all().item())
convolution_finite = bool(torch.isfinite(convolution_result).all().item())

print("duration_seconds:", round(elapsed_seconds, 3))
print("matrix_iterations:", iterations)
print("matrix_finite:", matrix_finite)
print("convolution_finite:", convolution_finite)
print(
    "peak_memory_mb:",
    round(torch.cuda.max_memory_allocated(device) / 1024**2, 2),
)

if iterations <= 0:
    raise RuntimeError("No CUDA matrix iterations completed")
if not matrix_finite:
    raise RuntimeError("CUDA matrix result contains invalid values")
if not convolution_finite:
    raise RuntimeError("CUDA convolution result contains invalid values")

print("CUDA_PYTORCH_SMOKE_PASS")
