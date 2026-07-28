# Front camera calibration

## Current state

The front USB camera is validated at 640×480, MJPEG input, and 15 FPS. The checked-in
`front_camera_info.yaml` is intentionally an uncalibrated placeholder: focal lengths are zero and
`ObservationStatus.camera_calibrated` remains false.

Do not use the placeholder calibration for projection, visual localization, metric geometry, or
camera-to-base transforms.

## Required preparation

1. Rigidly mount the camera in its final vehicle position.
2. Measure and record the checkerboard inner-corner dimensions and square size.
3. Install or vendor a ROS 2 Humble-compatible calibration tool. The current vehicle has
   `camera_calibration_parsers` but does not have the `camera_calibration` executable package.
4. Start `phase1_shadow.launch.xml` and calibrate `/camera/image_raw` at 640×480.
5. Replace `vehicle_bringup/config/front_camera_info.yaml` with the generated result.
6. Verify the camera name is `front_camera` and the focal lengths in `camera_matrix` are non-zero.
7. Set `require_calibration: true` in `phase1_shadow.yaml` only after the result is reviewed.

## Acceptance checks

- Reprojection error is recorded with the calibration artifact.
- Images show no unexpected crop, rotation, or mirrored orientation.
- The camera optical frame follows ROS optical-frame axis conventions.
- The physical transform from `base_link` to `front_camera_optical_frame` is measured separately.
- `ObservationStatus.camera_calibrated` is true after launch.

The calibration artifact is configuration, not a generated build product, and should be committed
with the camera serial number, resolution, mount revision, and calibration date in the commit
message or accompanying release record.
