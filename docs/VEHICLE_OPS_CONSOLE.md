# Vehicle Ops Console 使用说明

## 定位

Vehicle Ops Console 是新工程内独立实现的内部运维调试工具，不修改也不依赖旧 `rhzd_assist`。它由两部分组成：

- C++17 ROS 2 节点 `vehicle_ops_api`：订阅统一状态 Topic，调用白名单 Service，并提供轻量 HTTP API；
- 完全离线的响应式 Web 页面：不依赖 npm、CDN、rosbridge 或宿主机 Python Web 环境。

浏览器不直接连接 ROS 2，不访问底盘串口，也不能发布最终 `/cmd_vel`。当前版本允许任务文本、Episode Start/Stop、Safe Stop、VLA Shadow 单步调试，以及严格白名单化的工程 Job。它不提供任意 Shell、Topic 或 Service 入口。

## 构建

在 Orin 上执行：

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/build.sh
```

`vehicle_bringup` 已声明对 `vehicle_ops` 的运行依赖，因此正常工程构建会一起编译控制台。

## 启动

建议先启动需要观察的 Shadow 链路：

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/run_phase1_shadow.sh
```

在另一个独立终端启动控制台：

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/run_vehicle_ops_console.sh
```

首次启动会创建：

```text
run/config/vehicle_ops.env
```

文件权限自动设为 `600`，其中包含随机 Operator Token。启动终端会显示访问地址和 Token，例如：

```text
Vehicle Ops Console URLs:
  http://10.101.70.232:8088
Operator token: <random-token>
```

在同一局域网浏览器打开：

```text
http://10.101.70.232:8088
```

只读状态和相机画面不需要 Token。点击右上角令牌按钮，将启动终端显示的 Token 保存到当前浏览器会话后，才能执行写操作。Token 只进入 `sessionStorage`，关闭浏览器会话后失效。

## 页面功能

控制台将功能拆分为四个顶部视图，避免所有表单和状态堆叠在同一长页面：

- **实时监控**：控制模式、四项核心状态、前视相机、链路健康和 Shadow 指标；
- **数据采集**：Episode Start/Stop、任务文本和 Safe Stop；
- **VLA 调试**：冻结当前 Observation、仅预处理、单次推理、原始 Action Tensor 和 Twist 解释；
- **工程工具**：受控任务中心、最近任务历史、日志查看、任务取消和调试命令预览。

切换视图不会启动或停止任何 ROS 节点，也不会丢失当前浏览器会话中的 Operator Token。

### 总览

页面每秒刷新一次，显示：

- Supervisor 控制模式、控制来源和移动安全状态；
- Observation、相机、里程计、IMU 与标定状态；
- Policy Provider、模型 ID 和最近推理延迟；
- Episode 状态、持续时间、消息数和图像数；
- Shadow 样本数、线速度 MAE 和角速度 MAE；
- 最新 Safety Event；
- Orin 内存、Load 和系统运行时间。

状态超过配置的 `stale_after_seconds` 后显示为 `STALE`，不会继续伪装成在线。

### 相机

API 保存最近一帧 `/camera/image_compressed`，浏览器定期请求：

```text
/api/camera/front.jpg
```

这不是额外的视频编码服务，不会复制完整视频流，只用于内部状态确认。没有相机 Topic 时页面保持占位状态。

### VLA 单步调试

VLA 调试页只在 `MODE_VLA_SHADOW` 和车辆静止时工作。它先冻结一份 `/vla/observation`，再由用户显式选择“仅预处理”或“单次推理”。页面显示原始/处理后图像、Tensor 元数据、完整 normalized Action Chunk、反归一化动作、Twist 解释和分阶段延迟。

所有工件保存在 `run/ops/debug/<run-id>/`。该链路不发布 `/cmd_vel`；默认 `smolvla-shadow-zero-v1` 适配器仍把动作映射为零 Twist。完整说明见 `docs/VLA_PIPELINE_INSPECTOR.md`。
### Episode

填写可选 Episode ID、任务描述和操作员后点击“开始记录”。页面调用：

```text
/vehicle/start_episode
```

点击“停止并封存”调用：

```text
/vehicle/stop_episode
```

如果 Episode Recorder 未启动，页面会明确显示服务不可用，不会自动启动其他 ROS 节点。

### VLA 任务

任务文本发布到 `/vla/task`。它只更新后续 Observation 使用的任务描述，不直接产生底盘控制命令。

### Safe Stop

点击安全停车后必须再次确认。API 只调用 Supervisor 的 `/vehicle/request_safe_stop`，不直接发布 Twist。该操作不是解除急停或恢复运动的入口。

### 数据工具

页面提供 Dataset Inspector、Episode Split 和 LeRobot Convert 命令生成器。当前 MVP 只生成并复制白名单命令，不允许浏览器执行任意 Shell。这样保留工具便利性，同时避免把一个通用远程终端暴露到车辆网络。

## 停止

在运行控制台的终端按 `Ctrl+C`。停止控制台不会停止相机、Episode Recorder、Policy Runtime、Nav2 或其他 ROS 节点。

## 配置

运行时配置位于 `run/config/vehicle_ops.env`：

```bash
VEHICLE_OPS_BIND_ADDRESS=0.0.0.0
VEHICLE_OPS_PORT=8088
VEHICLE_OPS_OPERATOR_TOKEN=<random-token>
VEHICLE_OPS_JOBS_ROOT=/home/wheeltec/vla_vehicle_platform/run/ops/jobs
```

修改端口或 Token 后重新启动控制台。示例文件为 `config/vehicle_ops.env.example`，示例不包含真实凭据。

如只允许 Orin 本机访问，可设置：

```bash
VEHICLE_OPS_BIND_ADDRESS=127.0.0.1
```

然后从开发电脑建立 SSH 隧道：

```powershell
ssh -L 8088:127.0.0.1:8088 wheeltec@10.101.70.232
```

浏览器访问 `http://127.0.0.1:8088`。

## HTTP API

| 方法 | 路径 | Token | 作用 |
|---|---|---:|---|
| GET | `/api/status` | 否 | 聚合车辆、Policy、Episode、Shadow 和主机状态 |
| GET | `/api/camera/front.jpg` | 否 | 返回最近一帧压缩图像 |
| POST | `/api/task` | 是 | 发布 `/vla/task` |
| POST | `/api/episode/start` | 是 | 调用 Episode Start Service |
| POST | `/api/episode/stop` | 是 | 调用 Episode Stop Service |
| POST | `/api/safe-stop` | 是 | 请求 Supervisor Safe Stop |
| POST | `/api/vla-debug/capture` | 是 | 冻结当前 Observation |
| POST | `/api/vla-debug/run` | 是 | 执行预处理或一次 Shadow 推理 |
| GET | `/api/vla-debug/runs/<run-id>/original.jpg` | 是 | 获取冻结的原始图像 |
| GET | `/api/vla-debug/runs/<run-id>/processed.jpg` | 是 | 获取 Provider 处理后图像 |
| POST | `/api/jobs` | 是 | 提交白名单工程任务 |
| GET | `/api/jobs` | 是 | 查询最近任务 |
| GET | `/api/jobs/<job-id>` | 是 | 查询任务状态 |
| GET | `/api/jobs/<job-id>/log` | 是 | 获取有界日志尾部 |
| POST | `/api/jobs/<job-id>/cancel` | 是 | 取消运行中的任务 |

Job API 的读写请求和其他写请求必须携带 `X-Ops-Token`。API 不提供任意 Topic 发布、任意 Service 调用、任意文件读取或 Shell 执行能力。

## 安全限制

- 当前 HTTP 服务没有 TLS，只允许在可信车辆局域网或 SSH 隧道内使用；
- Operator Token 不得提交到 Git、截图公开或放入 URL；
- 页面不提供 MANUAL、NAV2、VLA_ASSISTED 或 AUTONOMOUS 模式切换；
- 页面不提供手机方向控制，后续必须通过独立 Control Lease 和 Deadman 机制实现；
- 页面不启动 `wheeltec_robot_node`，也不访问 STM32 串口；
- VLA 仍不能发布最终 `/cmd_vel`。

## 故障排查

### 页面无法打开

```bash
ros2 node list | grep vehicle_ops
ss -lntp | grep 8088
curl http://127.0.0.1:8088/api/status
```

### 页面有状态但没有相机

```bash
ros2 topic hz /camera/image_compressed
ros2 topic echo /vehicle/observation_status --once
```

### 写操作返回 401

重新查看 `run/config/vehicle_ops.env`，在右上角令牌面板更新 Token。不要把 Token 附加到浏览器地址。

### Episode 服务不可用

确认 Phase 1 Shadow 或 Episode Recorder 已启动：

```bash
ros2 service list | grep episode
```