# ARM64 Orin Release Runner

## Role

The runner is a separate Jetson Orin host used only to build ARM64 release
artifacts. It must not be the production vehicle. Keep its JetPack, L4T, Ubuntu,
and ROS versions aligned with the vehicle.

The current target baseline is:

- architecture: `aarch64`
- Ubuntu: `22.04`
- ROS: `Humble`
- L4T: `36.4.3`
- Docker with NVIDIA Container Runtime

## Prepare the host

Clone the central repository on the runner and validate it:

```bash
git clone https://gitee.com/binglingfenzi/vla_vehicle_platform.git
cd vla_vehicle_platform
./scripts/prepare_orin_runner.sh
```

The runner user must be able to use Docker without sudo and needs at least
25 GiB free space for CUDA images and exported OCI archives.

## Gitee Go host Agent

Gitee generates a host-specific Agent installation command in the Gitee Go
host-management page. Run that generated command once on the independent Orin.
The registration token must not be committed to this repository.

Configure the release stage to execute from the checked-out repository:

```bash
./ci/orin-runner/release_job.sh "${RELEASE_VERSION}"
```

If no version is supplied, the release version is derived from Git. The job
prints `artifact_root` and ends with `ORIN_RELEASE_JOB_PASS`.

Expected artifacts:

```text
artifacts/releases/<version>/
|-- vla-vehicle-<version>-arm64.tar.zst
|-- vla-vehicle-<version>-arm64.tar.zst.sha256
|-- vla-policy-runtime-<version>-arm64.oci.tar.zst
|-- vla-policy-runtime-<version>-arm64.oci.tar.zst.sha256
|-- install_release.sh
|-- deploy_release.sh
|-- rollback_release.sh
`-- rollback_remote_release.sh
```

The Runner uses `flock` so two release jobs cannot build in the same checkout
at the same time.

## Hardware boundary

The runner validates ARM64 compilation and container construction. It does not
replace vehicle HIL tests for `/dev/video0`, STM32 serial communication, chassis
motion, thermal behavior, or safety acceptance.
