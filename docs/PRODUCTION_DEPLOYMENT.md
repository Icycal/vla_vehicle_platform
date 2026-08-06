# Production Release Deployment

## Release artifacts

Production vehicles receive binary artifacts, not the Git repository:

- `vla-vehicle-<version>-arm64.tar.zst`: merged ROS install, launch wrappers,
  production configuration defaults, Compose, systemd units, and manifest.
- `vla-policy-runtime-<version>-arm64.oci.tar.zst`: exported Policy Runtime
  image.
- SHA256 sidecars for both files.

The current Policy Runtime artifact contains the Mock Provider and is suitable
for Shadow infrastructure only. A persistent SmolVLA Provider will replace the
image without changing the ROS release layout.

## Build on the ARM64 runner

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/build_release.sh --version 0.2.0
```

For ROS-only validation without rebuilding the Policy Runtime image:

```bash
./scripts/build_release.sh --version 0.2.0-test --skip-policy-image
```

Release builds use `--merge-install`, `CMAKE_BUILD_TYPE=Release`, separate
build/install/log directories, and reject symlinks back to source or build
trees.

## One-time vehicle bootstrap

On the production vehicle, install the restricted deployment commands once:

```bash
cd /home/wheeltec/vla_vehicle_platform
sudo ./scripts/bootstrap_vehicle_deployer.sh
```

This installs:

```text
/usr/local/sbin/vla-install-release
/usr/local/sbin/vla-rollback-release
/etc/sudoers.d/vla-deploy
```

The CI deploy user may invoke only these fixed deployment entry points without
a password; it is not granted unrestricted passwordless shell access.

## Automated deployment

From the ARM64 runner:

```bash
./scripts/deploy_release.sh \
  --target wheeltec@10.101.70.232 \
  --archive artifacts/releases/0.2.0/vla-vehicle-0.2.0-arm64.tar.zst \
  --policy-image artifacts/releases/0.2.0/vla-policy-runtime-0.2.0-arm64.oci.tar.zst \
  --non-interactive
```

The installer:

1. verifies SHA256 sidecars when present;
2. rejects unsafe archive paths, escaping symlinks, and special files;
3. verifies the internal release manifest and file checksums;
4. accepts only the current ARM64 Shadow/Mock release profile;
5. installs the new version beside existing releases;
6. preserves `/etc/vla-vehicle` local configuration;
7. loads and verifies the required Policy Runtime image;
8. atomically updates `/opt/vla-vehicle/current`;
9. restarts the Shadow services;
10. restores the previous link, or removes the first link, if restart fails.

## Layout

```text
/opt/vla-vehicle/releases/<version>
/opt/vla-vehicle/current
/opt/vla-vehicle/previous
/etc/vla-vehicle
/var/lib/vla-vehicle
/var/log/vla-vehicle
```

Default configuration updates are written as `*.dist`. Existing active
configuration files are never overwritten automatically.

## Rollback

From the runner or operator workstation:

```bash
./scripts/rollback_remote_release.sh wheeltec@10.101.70.232
```

The command swaps `current` and `previous`, then restarts only the Policy
Runtime and ROS Shadow services. It does not start `wheeltec_robot_node`.

## Staging test

The installer supports non-root staging by overriding roots:

```bash
VLA_OPT_ROOT=/tmp/vla-deploy/opt/vla-vehicle \
VLA_ETC_ROOT=/tmp/vla-deploy/etc/vla-vehicle \
VLA_VAR_ROOT=/tmp/vla-deploy/var/lib/vla-vehicle \
VLA_SYSTEMD_ROOT=/tmp/vla-deploy/systemd \
VLA_SKIP_SYSTEMD=1 \
./scripts/install_release.sh \
  --archive artifacts/releases/0.2.0-test/vla-vehicle-0.2.0-test-arm64.tar.zst \
  --activate \
  --no-systemd
```
