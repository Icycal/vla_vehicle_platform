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
- 增加模型无关 `vehicle.observation.v1`、C++ Observation Adapter 和 Observation 有效期检查。
- Policy Gateway 从任务字符串输入升级为图像、状态和任务的完整 Observation 输入。
- 增加 C++ Shadow Evaluator、单次动作误差和累计 MAE 指标。
- 增加常驻 SmolVLA Policy Provider、GPU 预热和真实 Protobuf 推理 Smoke Client。
- 增加默认零输出与显式仿射两种 Shadow Action Adapter，非 Shadow 模式强制拒绝。
- Policy Runtime 支持健康检查与长推理并发，C++ Gateway 改为多线程回调避免阻塞相机输入。

- 增加 C++ `vehicle.dataset.v1` Episode Exporter，按 Observation ID 关联图像、状态、PolicyAction 和 ShadowComparison。
- 增加 C++ Observation Replay，仅向隔离 Replay 话题刷新并发布 Observation。
- Policy Gateway 支持配置 Observation 输入话题，用于安全 Rosbag Replay。
- 增加配置驱动的 LeRobot v3 Dataset 转换器、Ackermann 特征映射和容器内加载验证。
- 增加 Orin Smoke 与 x86 CUDA Fine-tune 双训练 Profile、离线 policy overlay 和训练制品清单。
- 增加 Jetson PyTorch `torch.distributed` 兼容入口和 x86 SmolVLA 训练镜像定义。
- 增加车辆中间格式与 LeRobot v3 Dataset Inspector、JSON/Markdown 质量报告和训练就绪门禁。
- 增加确定性 Episode 级 train/validation/test Split Manifest 和按 Split 转换 LeRobot 入口。
- 增加独立 C++ ROS 2 Vehicle Ops API 和完全离线响应式 Web 运维控制台。
- 增加 Token 保护的任务发布、Episode Start/Stop、Safe Stop 和 Dataset 命令生成器。

### Validation

- Orin NX 实测相机图像约 12 FPS，Observation Ready，分辨率 640×480。
- 约 7 秒测试 Episode 记录 29 张压缩图像和 597 条状态/控制消息。
- Observation、Policy Action 和 Shadow Comparison 的关联 ID 已完成端到端验证。
- 常驻 SmolVLA 热推理约 0.92--1.05 秒，推理期间健康检查约 0.20 秒返回。
- 真实相机到 SmolVLA Provider 的 ROS Shadow 链路通过，最终 `/cmd_vel` 保持零。
- 重叠预测请求会被拒绝，不会累积过期图像队列。
- `container-shadow-001` 导出 49 个完整训练帧，跳过 1 个未关联的 Observation。
- 真实 Observation 经 Replay、SmolVLA 和 PolicyAction 的离线链路通过，最终 `/cmd_vel` 保持零。
- LeRobot 单 Episode 49 帧和双 Episode 98 帧转换、重新加载、FPS 错配拒绝及覆盖拒绝均通过。
- Orin NX 完成 5-step SmolVLA 真实反向传播：约 100M 可训练参数，后四步约 0.46 秒/step。
- 车辆格式与 LeRobot 格式质量报告一致识别全零动作、状态无效和 Episode 不足，训练就绪均为 false。
- Episode Split 相同种子字节一致，不同种子改变分配，重复 Episode ID 被拒绝且 Split 间无泄漏。
- Vehicle Ops 静态页面、HTTP 状态接口和白名单写操作在 Orin NX 上完成构建与访问验证。
- 训练 Smoke 峰值 RAM 约 7.2GB、GPU 利用率 99%、结温约 49°C、输入功耗约 10.2W。
- 关联测试 Episode 记录 35 组 Observation/Prediction/Comparison 和 28 张图像。
- 测试期间最终 `/cmd_vel` 始终为零，未启动 `wheeltec_robot_node`。

### Pending

- 当前相机内参文件为未标定占位值，`camera_calibrated=false`。
- 车端缺少可执行的 `camera_calibration` 工具，正式数据采集前必须完成几何标定。
