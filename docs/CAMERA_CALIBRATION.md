# 赤兔前视相机标定

## 网页闭环

“相机标定”页面在不启动底盘的前提下完成：启动前视相机、创建会话、采集棋盘格、计算内参与畸变、检查误差和去畸变预览、备份并保存 YAML、重启相机、验证 `/camera/camera_info`。

仓库中的 `ros_ws/src/vehicle_bringup/config/front_camera_info.yaml` 默认是未标定占位文件。`fx`、`fy` 为零时，网页与 `ObservationStatus.camera_calibrated` 会显示未标定。

## 使用准备

1. 相机固定在最终安装位置。
2. 数清棋盘的内部角点列数和行数，不是方格数。
3. 用尺实测单格边长并换算成米，不能直接套用教程示例。
4. 保证棋盘平整、完整入镜、光照均匀。

原厂棋盘图约为 7×9 个方格，即 6×8 个内部角点；打印缩放会改变格长，因此网页只默认 6×8，格长必须现场填写。

## 操作步骤

1. 进入“相机标定”，点击“启动前视相机”。
2. 填写角点列数、行数、实测格长、最小样本数，可选填相机序列号。
3. 创建会话后，从左/中/右、上/中/下、近/远及不同倾角采集至少 12 组图像。重复姿态会被拒绝。
4. 点击“计算相机内参”，检查 `fx/fy/cx/cy`、畸变参数、RMS、重投影误差和去畸变对照。
5. 检查通过后勾选确认并点击“保存并应用标定”。系统会备份旧文件、原子写入新文件、重启 `front_camera` 并验证新 CameraInfo。
6. 页面显示“验证通过”才表示完整闭环成功。

通常重投影误差小于 1 像素较理想，但仍需结合去畸变后的直线和画面边缘效果判断。

## 通用路径

模块不写死 Linux 用户或部署目录。参数如下：

```yaml
camera_calibration_path: ""
camera_calibration_metadata_path: ""
camera_component_id: "front_camera"
camera_calibration_verify_timeout_seconds: 8.0
```

路径留空时由 `project_root` 推导：

```text
<project_root>/ros_ws/src/vehicle_bringup/config/front_camera_info.yaml
<project_root>/ros_ws/src/vehicle_bringup/config/front_camera_info.meta.json
```

其他部署方式可以显式指定可写路径，但相机的 `camera_info_url` 必须指向同一个 YAML，否则应用后的 CameraInfo 验证会失败。

## API

只读：

```text
GET /api/camera-calibration/status
GET /api/camera-calibration/preview.jpg
GET /api/camera-calibration/undistorted.jpg
```

需要 Operator Token：

```text
POST /api/camera-calibration/start
POST /api/camera-calibration/capture
POST /api/camera-calibration/compute
POST /api/camera-calibration/apply
POST /api/camera-calibration/reset
```

## 验收

- 图像分辨率与 YAML 一致；
- `camera_name` 为 `front_camera`；
- `K[0]`、`K[4]` 大于零；
- 相机重启后 `/camera/camera_info` 验证通过；
- `ObservationStatus.camera_calibrated` 为 `true`；
- 去畸变预览无明显异常拉伸。

本功能是相机内参与镜头畸变标定，不包含相机到车体的外参标定。视觉定位、BEV 或精确空间投影仍需单独规划外参标定。
