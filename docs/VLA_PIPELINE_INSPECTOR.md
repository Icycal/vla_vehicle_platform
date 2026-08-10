# VLA Pipeline Inspector 使用与设计说明

## 1. 目标

VLA Pipeline Inspector 是 Vehicle Ops Console 中的 Shadow-only 单步调试台，用于回答以下工程问题：

- 当前送入模型的到底是哪一帧图像、什么任务文本和什么车辆状态；
- 图像预处理后的尺寸、颜色空间、Tensor shape、dtype、device 和数值范围是什么；
- 模型一次推理返回的完整归一化 Action Chunk 是什么；
- 经过 Provider 反归一化后的动作预览是什么；
- 当前动作适配器会把动作解释成什么 `linear_x` / `angular_z`；
- 预处理、模型推理、后处理和总耗时分别是多少。

它只用于观测和诊断，不是车辆控制入口。调试节点不发布 `/cmd_vel`，不启动底盘驱动，不访问 STM32 串口。

## 2. 架构

```text
/camera/image_compressed + vehicle state + /vla/task
                         |
                         v
                 observation_adapter
                         |
                  /vla/observation
                         |
                [冻结 Observation]
                         |
                         v
              vla_debug_orchestrator (C++)
                 |                 |
      preprocess |                 | inference
                 v                 v
       Policy Transport DebugRequest
                         |
                         v
            Policy Runtime / Provider (Python)
                         |
        DebugResponse + processed JPEG + JSON
                         |
                         v
          run/ops/debug/<run-id>/ + Ops HTTP API
                         |
                         v
               Vehicle Ops Console 浏览器
```

职责边界：

- `vehicle_vla_debug`：冻结 ROS Observation、安全门禁、调用 Provider Debug API、保存工件；
- `vehicle_policy_transport`：承载与具体模型无关的 DebugRequest/DebugResponse；
- Policy Runtime：执行 Provider 私有预处理、一次模型调用和后处理；
- `vehicle_ops_api`：只暴露固定调试 Service 和固定图片文件，不提供任意 ROS、Shell 或文件访问；
- Web UI：展示输入、输出和延迟，不直接连接 ROS 2。

## 3. 使用前提

1. SmolVLA Runtime 容器处于 healthy 状态；
2. Phase 1 Shadow 已启动并收到相机 Observation；
3. Supervisor 当前模式为 `MODE_VLA_SHADOW`；
4. 车辆静止，最近 `/cmd_vel` 没有超过安全阈值的非零命令；
5. Vehicle Ops Console 已启动；
6. 浏览器已填写运行时 Operator Token。

推荐检查：

```bash
cd /home/wheeltec/vla_vehicle_platform

docker ps --filter name=vla-smolvla-runtime
./scripts/ros_env.sh vla -- ros2 node list
./scripts/ros_env.sh vla -- ros2 topic echo /vehicle/system_state --once
./scripts/ros_env.sh vla -- ros2 topic echo /vla/observation --once --field observation_id
```

不得启动 `wheeltec_robot_node` 来完成本调试。

## 4. 页面操作

浏览器打开：

```text
http://10.101.70.232:8088
```

进入 **VLA 调试** 页面后：

### 4.1 冻结当前输入

1. 填写任务文本，例如 `move forward and avoid obstacles`；
2. 点击 **冻结当前 Observation**；
3. 页面生成唯一 `run_id`，并显示冻结的原始相机图像和 Observation JSON。

冻结后即使实时相机继续更新，本次预处理和推理仍使用同一份快照，便于复现和对比。

### 4.2 仅执行预处理

点击 **只运行预处理**：

- 不调用模型推理；
- 显示处理后的图像；
- 显示模型输入 Tensor 的 shape、dtype、device、min/max/mean；
- 保存 `preprocess.json`；
- 不产生动作，更不会发布控制命令。

### 4.3 执行一次推理

点击 **执行单次推理** 并确认：

- 只对冻结的 Observation 运行一次显式推理；
- 显示完整归一化 Action Chunk；
- 显示反归一化动作预览；
- 显示动作适配后的 `Linear X` 和 `Angular Z`；
- 显示分阶段延迟和完整 JSON；
- 不发布控制命令。

## 5. 输出解释

### 5.1 Normalized Action Chunk

`raw_output.normalized_action_chunk` 是模型直接返回的归一化动作 Tensor。当前 SmolVLA Base 实测 shape 为：

```text
[1, 50, 6]
```

含义是 1 个 batch、50 个时间步、每步 6 个动作维度。它是最接近模型原始接口的输出，应优先用于模型兼容性、数值分布和回归对比。

### 5.2 Denormalized Actions

`raw_output.denormalized_actions.values` 是 Provider 根据模型特征统计量反归一化后的动作。当前调试预览窗口配置为 8 步，因此页面可能显示：

- 50 行 normalized 原始输出；
- 前 8 行同时存在 denormalized 输出；
- 第 9 行以后标记为“仅原始输出（超出执行预览窗口）”。

这不是数据丢失，而是完整模型输出与有限执行预览窗口的区别。

### 5.3 Interpreted Twist

`interpreted_output.twist_actions` 是动作适配器对反归一化动作的车辆语义解释。当前默认适配器为：

```text
smolvla-shadow-zero-v1
```

它故意把所有候选动作映射为零 `Twist`。因此看到真实非零模型 Action Tensor、但 `linear_x=0`、`angular_z=0` 是预期安全行为，不代表模型没有输出。

任何非零动作映射都必须在独立标定、限幅、安全门禁和 Shadow 验证后再启用。

### 5.4 Latency

`latency_ms` 包含：

- `preprocessing`：图像解码、缩放、Tensor 构造和设备搬运；
- `inference`：SmolVLA 模型调用；
- `postprocessing`：动作抽取、反归一化和适配；
- `total`：Provider 端完整调试处理时间。

2026-08-10 在 Orin NX 16GB 上的真实单帧验证约为 1 秒总耗时；延迟会受功耗模式、GPU 时钟、后台 Shadow 请求、温度和首次运行缓存影响。

## 6. 工件目录

每次冻结创建：

```text
run/ops/debug/<run-id>/
```

文件包括：

```text
observation.json
camera-original.jpg
preprocess.json
inference.json
camera-processed.jpg
```

`preprocess.json` 和 `inference.json` 只有在对应阶段成功后才出现。目录属于运行数据，不应提交到 Git。

## 7. HTTP API

所有调试 API 都要求请求头：

```text
X-Ops-Token: <operator-token>
```

### 冻结 Observation

```bash
curl -H 'X-Ops-Token: <operator-token>' \
  -H 'Content-Type: text/plain' \
  --data 'move forward and avoid obstacles' \
  http://127.0.0.1:8088/api/vla-debug/capture
```

### 仅预处理

```bash
curl -H 'X-Ops-Token: <operator-token>' \
  -H 'Content-Type: application/json' \
  --data '{"run_id":"vla-debug-...","stage":"preprocess"}' \
  http://127.0.0.1:8088/api/vla-debug/run
```

### 单次推理

```bash
curl -H 'X-Ops-Token: <operator-token>' \
  -H 'Content-Type: application/json' \
  --data '{"run_id":"vla-debug-...","stage":"inference"}' \
  http://127.0.0.1:8088/api/vla-debug/run
```

### 获取图片

```text
GET /api/vla-debug/runs/<run-id>/original.jpg
GET /api/vla-debug/runs/<run-id>/processed.jpg
```

图片接口只接受以 `vla-debug-` 开头的安全 ID 和两个固定文件名，不能读取任意路径。

## 8. Provider Busy 行为

常驻 Policy Gateway 会继续执行后台 Shadow 预测，因此显式调试请求可能刚好遇到 Provider 正忙。

`vla_debug_orchestrator` 只对包含 `provider is busy` 的错误执行有限重试：

```yaml
provider_busy_retry_count: 5
provider_busy_retry_delay_ms: 350
```

其他错误立即返回。重试耗尽后页面显示失败，不会无限排队，也不会把故障伪装为成功。

## 9. Provider 可替换契约

未来接入 OpenVLA、其他 LeRobot Policy 或自研 VLA 时，ROS 和 UI 不依赖其私有 Python 类。新 Provider 只需实现统一的：

```python
debug(observation, debug_run_id, stage) -> DebugResponse
```

并返回 `vehicle.vla.debug.v1` JSON，至少包含：

- `provider_id`、`model_id`、`schema_version`、`stage`；
- Observation 元数据；
- 预处理信息；
- 原始动作输出；
- 可选的反归一化和解释输出；
- 分阶段延迟；
- `safety.operation_mode=shadow`；
- `safety.publishes_control=false`。

Provider 可以增加私有字段，但不能改变安全字段语义，也不能从 Debug API 发布车辆控制命令。

## 10. 安全边界

- 只允许 `MODE_VLA_SHADOW`；
- 车辆最近存在非零 `/cmd_vel` 时拒绝冻结或运行；
- 调试节点没有 `/cmd_vel` Publisher；
- HTTP API 不提供控制模式切换；
- 浏览器不能调用任意 Service、Topic、Shell 或读取任意文件；
- 调试工件仅写入 `run/ops/debug/`；
- 当前动作适配器固定输出零 Twist；
- 不启动底盘驱动，不访问 STM32。

## 11. 停止

在各自启动终端按 `Ctrl+C` 是首选方式。

停止 Phase 1 Shadow 后，摄像头、Observation 和调试 ROS Service 会停止；停止 Ops Console 只会关闭网页/API；停止 SmolVLA Runtime 会使 Policy 调试请求不可用。

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/stop_smolvla_runtime.sh
```

## 12. 故障排查

### 页面提示必须处于 Shadow 模式

检查：

```bash
./scripts/ros_env.sh vla -- ros2 topic echo /vehicle/system_state --once
```

由具备权限的运维流程显式请求 `MODE_VLA_SHADOW`，不要通过网页绕过 Supervisor。

### 没有可冻结的 Observation

检查：

```bash
./scripts/ros_env.sh vla -- ros2 topic hz /camera/image_compressed
./scripts/ros_env.sh vla -- ros2 topic echo /vehicle/observation_status --once
./scripts/ros_env.sh vla -- ros2 topic echo /vla/observation --once
```

### Provider 不可用

检查：

```bash
docker ps --filter name=vla-smolvla-runtime
docker logs --tail 100 vla-smolvla-runtime
```

### 页面返回 401

重新从 `run/config/vehicle_ops.env` 获取 Token，并在右上角令牌面板更新。不要把 Token 放入 URL、文档或 Git。