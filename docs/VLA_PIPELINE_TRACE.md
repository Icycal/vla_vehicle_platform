# VLA 实时 Pipeline Trace 使用与设计说明

## 1. 定位

实时 Pipeline Trace 是独立、只读、被动的整车 VLA 链路观测能力。它通过现有 ROS 2 Topic 汇总传感器、Observation、Policy、动作运行时、控制仲裁、安全和 Shadow 结果，不创建第二条控制链，也不为了页面刷新额外调用模型。

核心组件：

```text
pipeline_trace_aggregator (C++17 / rclcpp)
```

输出 Topic：

```text
/vla/pipeline_trace
```

消息类型：

```text
vehicle_interfaces/msg/PipelineTrace
```

Trace 本身始终声明：

```text
publishes_control=false
```

这表示聚合节点和调试页面不发布控制命令。Trace 中 `Safety Guard` 阶段的 `publishes_control=true` 只描述被观察组件本身是最终控制边界，不表示调试工具拥有该能力。

## 2. Vehicle Ops 页面

进入 Vehicle Ops Console 的 **VLA 调试** 页面后有三个模式。

### 2.1 实时链路

默认模式，显示：

- 实时前视相机；
- Trace ID、Observation ID、Provider 和模型；
- 11 个可横向滚动的链路阶段；
- 每个阶段的 `LIVE / READY / RUNNING / STALE / REJECTED / FAILED` 状态；
- 输入摘要、输出摘要、数据年龄和阶段延迟；
- PolicyAction、Safety Guard 和 Shadow Evaluation 摘要；
- 点击阶段后显示结构化 Detail JSON。

页面每秒读取一次缓存的 Trace。页面刷新不会触发 SmolVLA 推理，因此不会增加 GPU 负载或与 Policy Gateway 争用 Provider。

“最近一次模型处理图像”来自快照预处理结果。被动实时链路不会为了生成这张图而偷偷运行额外预处理。

### 2.2 快照单步

冻结一份 Observation 后检查模型内部阶段：

1. 冻结输入；
2. 契约检查；
3. Decode / Resize / Tensor 预处理；
4. VLA 推理；
5. 动作反归一化；
6. Ackermann / Twist 动作解释；
7. Shadow 安全边界确认。

后端仍只有三个显式操作：冻结、仅预处理、单次推理。七个阶段是对返回结果的工程语义展开，不会伪造七次独立模型调用。

### 2.3 Trace 历史

Ops API 在内存中保存最近 30 个不同 Observation 的 Trace：

- 点击条目查看当时所有阶段的完整 JSON；
- `FAILED` 或 `REJECTED` Trace 标记为 `CHECK`；
- Ops Console 重启后内存历史清空；
- Episode Recorder 仍负责长期持久化车辆数据。

## 3. 实时阶段

| 顺序 | Stage ID | 组件 | 观测内容 |
|---:|---|---|---|
| 1 | `sensor_capture` | `front_camera + image_transport` | 压缩相机帧、格式、字节数、新鲜度 |
| 2 | `observation_health` | `observation_monitor` | Camera/Odom/IMU Ready、标定和分辨率 |
| 3 | `observation_assembly` | `observation_adapter` | Observation ID、任务、图像和状态数量 |
| 4 | `contract_validation` | `vehicle.observation.v1` | Schema、图像、任务和有效状态检查 |
| 5 | `model_runtime` | Policy Provider | Provider、模型、协议、推理耗时和运行状态 |
| 6 | `policy_output` | `vla_policy_gateway` | PolicyAction、请求 ID、动作数量和首个动作 |
| 7 | `action_runtime` | `vla_action_runtime` | Action Chunk 到候选 Twist |
| 8 | `control_mux` | `vla_control_mux` | 当前控制来源和被选候选命令 |
| 9 | `safety_guard` | `vla_safety_guard` | Safety Rule、最终 Twist 和控制边界 |
| 10 | `shadow_evaluation` | `shadow_evaluator` | 预测/实际误差和阈值判断 |
| 11 | `trace_recording` | `episode_recorder` | Episode 状态和记录计数 |

模型内部的图像 Decode、Tensor 构造、归一化、推理和反归一化无法仅依靠 ROS 外部 Topic 精确观测，因此由“快照单步”返回真实 Provider 数据，而不是在实时 Trace 中猜测。

## 4. 状态语义

| 状态 | 含义 |
|---|---|
| `WAITING` | 尚未收到该阶段数据 |
| `LIVE` | 连续实时数据正在更新 |
| `READY` | 最近结果有效且处于新鲜度阈值内 |
| `RUNNING` | Provider 正在加载或阶段处理中 |
| `STALE` | 收到过数据，但超过阶段新鲜度阈值 |
| `SKIPPED` | 当前模式明确跳过该阶段 |
| `REJECTED` | 契约、安全或质量门禁拒绝结果 |
| `FAILED` | 组件或运行时错误 |

默认新鲜度阈值：

```yaml
pipeline_trace_aggregator:
  ros__parameters:
    publish_frequency: 2.0
    stale_seconds: 3.0
    camera_stale_seconds: 1.0
    command_stale_seconds: 0.75
    policy_action_stale_seconds: 5.0
    shadow_stale_seconds: 5.0
```

SmolVLA 在当前 Orin NX 上约 1 秒完成 Provider 推理，但端到端 PolicyAction 实测约 `0.25 Hz`。因此 PolicyAction 和 Shadow 使用 5 秒阈值，避免把正常的异步推理误判为过期，同时仍能在链路真正停止后及时显示 `STALE`。

## 5. 异步 Observation 说明

Observation Adapter 以约 5 Hz 生成 Observation，而当前 SmolVLA PolicyAction 低于该频率。模型返回时，实时 Observation Head 通常已经前进。

因此 Trace 不要求：

```text
PolicyAction.observation_id == 当前最新 Observation ID
```

只要 PolicyAction 在新鲜度阈值内，就标记为 `READY`，并显示：

```text
Policy output is fresh; Observation advanced during inference
```

原始 `PolicyAction.observation_id` 仍保留在 Detail JSON 中，可用于严格回放和数据关联。

## 6. HTTP API

实时 Trace API 是只读接口，不需要 Operator Token。

### 最新 Trace

```text
GET /api/pipeline/live
```

返回单个 `PipelineTrace` JSON，并增加：

```text
received_age_ms
```

用于判断 Ops API 收到 Trace 后经过了多久。

### 最近 Trace

```text
GET /api/pipeline/history
```

返回：

```json
{
  "traces": []
}
```

只包含内存中最近 30 个不同 Trace ID。

### 示例

```bash
curl http://127.0.0.1:8088/api/pipeline/live
curl http://127.0.0.1:8088/api/pipeline/history
```

## 7. ROS 检查

```bash
cd /home/wheeltec/vla_vehicle_platform

./scripts/ros_env.sh vla -- ros2 node list | grep pipeline_trace
./scripts/ros_env.sh vla -- ros2 topic info /vla/pipeline_trace
./scripts/ros_env.sh vla -- ros2 topic echo /vla/pipeline_trace --once
```

期望节点：

```text
/pipeline_trace_aggregator
```

## 8. 故障示例

### Contract Validation 为 REJECTED

常见原因：

- `/vla/task` 为空；
- Observation Schema 不正确；
- Observation 没有图像；
- 相机帧为空。

点击阶段查看 `schema_valid`、`image_valid` 和 `task_valid`。

### Model Runtime 为 FAILED

常见原因：

- SmolVLA Runtime 容器未启动；
- Unix Socket 不存在或连接被拒绝；
- 模型加载失败；
- Provider 内部错误。

检查：

```bash
docker inspect --format '{{.State.Health.Status}}' vla-smolvla-runtime
docker logs --tail 100 vla-smolvla-runtime
ls -l run/policy/policy.sock
```

### Policy Output 为 STALE

表示超过 `policy_action_stale_seconds` 没有新 PolicyAction。它不等同于 Observation ID 与实时 Head 不一致。

检查：

```bash
./scripts/ros_env.sh vla -- ros2 topic hz /vla/policy_action
./scripts/ros_env.sh vla -- ros2 topic echo /vla/policy_state --once
```

### Safety Guard 为 REJECTED

点击阶段查看：

- `active_rule`；
- `severity`；
- Selected Twist；
- Final Twist。

Trace 只显示 Safety Guard 的判定，不能清除 Safety Event 或绕过安全规则。

## 9. 可替换性

Pipeline Trace 只依赖稳定车辆消息和阶段 ID，不依赖 SmolVLA Python 类型。未来替换 OpenVLA 或其他 Provider 时：

- `model_runtime` 继续使用统一 `PolicyStatus`；
- `policy_output` 继续使用统一 `PolicyAction`；
- Provider 私有 Tensor 继续通过 `vehicle.vla.debug.v1` 快照结果展示；
- UI 不直接导入 LeRobot、OpenVLA 或模型私有 SDK。

新增 Provider 可以扩展 Detail JSON，但不能改变 `publishes_control=false` 的调试工具边界。

## 10. 当前限制与后续演进

当前版本实现被动实时预览，不实现自动连续 Debug inference。后续如增加主动模型检查，应满足：

- 用户显式开启；
- 最大频率不超过 1 Hz；
- Provider Busy 时跳帧，不积压请求；
- 页面关闭或超时后自动停止；
- 与正常 Policy Gateway 推理区分统计；
- 仍然保持 Shadow-only 和零控制发布。

实时 Trace 历史当前保存在 Ops API 内存中。需要跨重启查询时，应写入独立 Trace Store 或从 Episode 数据离线重建，而不是让浏览器访问任意文件。