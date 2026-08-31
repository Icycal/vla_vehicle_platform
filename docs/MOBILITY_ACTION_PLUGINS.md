# 赤兔车型动作插件

赤兔以 Action Schema 连接数据采集、SmolVLA 训练、推理输出和底盘控制。模型输出是数值数组；数组语义由数据集和模型 Manifest 固定，插件只能按相同契约解释，不能在部署时重新定义。

## 数据流

```text
人工/Nav2 Twist
  -> vehicle_action_capture
  -> /chitu/action/target + /chitu/action/executed
  -> Episode -> vehicle.dataset.v1 -> LeRobot
  -> SmolVLA
  -> generic PolicyAction action_vectors
  -> vla_action_runtime Mobility Adapter
  -> /vla/cmd_vel_raw -> Mux -> Safety Guard -> /cmd_vel
  -> wheeltec_robot_node -> STM32
```

WheelTec Vendor 驱动继续负责串口协议、校验、里程计、IMU 和电池数据。车型插件不复制厂商协议。

## 内置插件

插件按模型动作协议选择，不按车辆名称选择：

| 模型 Action Schema | 应选择插件 |
|---|---|
| `vehicle.twist_chunk.v1` | `chitu.mobility.twist` |
| `chitu.action.differential.v1` | `chitu.mobility.twist` |
| `chitu.action.mecanum.v1` | `chitu.mobility.twist` |
| `chitu.action.ackermann.v1` | `chitu.mobility.ackermann` |

因此，阿克曼车型如果采集和训练的是 `linear_x + angular_z`，仍应使用 Twist 插件。只有模型直接输出 `speed_mps + steering_angle_rad` 时才使用 Ackermann 插件。模型管理页面会自动显示推荐插件；选错时禁用激活，API 和激活脚本也会再次拒绝。

```bash
./scripts/mobility_plugin.sh list
./scripts/mobility_plugin.sh show chitu.mobility.ackermann
```

激活 Twist：

```bash
./scripts/mobility_plugin.sh activate chitu.mobility.twist
```

激活阿克曼：

```bash
./scripts/mobility_plugin.sh activate chitu.mobility.ackermann
```

激活结果写入：

```text
run/config/mobility.yaml
run/config/mobility.json
```

切换前必须停车并重启 Runtime Core。阿克曼参数位于 `ros_ws/src/vehicle_bringup/config/mobility_ackermann.yaml`，正式控制前必须标定轴距，并确认 WheelTec 固件的 `angular.z` 表示 `yaw_rate` 还是 `steering_angle`。

## 训练数据

Twist 数据使用：

```text
config/lerobot_twist_dataset.json
```

阿克曼数据使用：

```text
config/lerobot_ackermann_dataset.json
```

导出器优先使用 `/chitu/action/executed`，旧 Episode 没有通用 Action 时回退到 `shadow.executed_twist`。

## 模型 Manifest

训练完成后，将数据集 Action Descriptor 写入模型：

```bash
./scripts/attach_model_action_manifest.sh \
  /path/to/trained/model \
  datasets/lerobot/<dataset-name>
```

模型激活时：

- 有 Action Descriptor：启用 `SMOLVLA_ACTION_ADAPTER=manifest`；
- 无 Action Descriptor：保持安全的 `zero` Codec；
- 模型 Schema 不在当前 Mobility Plugin 的 `accepts` 中：拒绝激活。

## 扩展车型

外部插件 Manifest 放入：

```text
run/plugins/mobility/<plugin>/plugin.json
```

第一版 Runtime 内置 `twist` 和 `ackermann` 转换。麦克纳姆可使用 `chitu.action.mecanum.v1` 和 Twist 输出；需要新运动学或新厂商协议时，应增加独立适配节点与 Manifest，不修改赤兔核心消息、Recorder 或 SmolVLA Provider。
