# Changelog

本项目遵循语义化版本思路，正式发布前版本保持 `0.x`。

## 0.1.0 - 2026-07-28

### Added

- 建立绿色 ROS 2 Humble 工作区和 C++17 工程规则。
- 增加 Supervisor、Mock Policy Gateway、Action Runtime、Control Mux 和 Safety Guard。
- 增加版本化消息、服务和动作接口。
- 增加 XML Phase 0 Launch、YAML 参数和隔离构建脚本。
- 导入 Nav2 Humble、Wheeltec 底盘、消息和串口源码快照。
- 完成车端零速 `/cmd_vel`、Shadow 模式和 SafeStop 验证。

### Safety

- Phase 0 不启动底盘串口节点。
- Safety Guard 作为唯一最终 `/cmd_vel` 发布者。

### Known Issues

- Wheeltec Vendor 包许可证声明尚未明确。
- Vendor 串口和底盘代码仍有既有编译警告。

## Unreleased

### Added

- 增加前视 USB 相机固定源码依赖和 640×480/15 FPS Shadow 启动配置。
- 增加 C++ Observation Monitor、Observation 健康接口和相机标定状态。
- 增加 C++ Episode Recorder、Start/Stop 服务和通用 Episode 状态接口。
- Episode 以 rosbag2 加 `vehicle.episode.v1` Manifest 保存，并以 5 FPS 记录 JPEG 图像。
- 增加 Phase 1 Shadow 启动、相机检查和 Episode 操作脚本。

### Validation

- Orin NX 实测相机图像约 12 FPS，Observation Ready，分辨率 640×480。
- 约 7 秒测试 Episode 记录 29 张压缩图像和 597 条状态/控制消息。
- 测试期间最终 `/cmd_vel` 始终为零，未启动 `wheeltec_robot_node`。

### Pending

- 当前相机内参文件为未标定占位值，`camera_calibrated=false`。
- 车端缺少可执行的 `camera_calibration` 工具，正式数据采集前必须完成几何标定。
