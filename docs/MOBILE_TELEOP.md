# 手机局域网遥控与训练数据采集

## 1. 入口

手机连接车辆所在可信局域网后打开：

```text
http://<vehicle-ip>:8088/teleop
```

该页面是 Vehicle Ops 提供的响应式移动 Web App，不要求 Android 或 iOS 原生安装。若需要浏览器完整 PWA 安装与离线缓存，应在 Vehicle Ops 前增加 HTTPS 反向代理；普通局域网 HTTP 不影响遥控和采集功能。

## 2. 控制链路

手机不直接发布 `/cmd_vel`，也不访问底盘串口：

```text
Mobile Web App
  -> Vehicle Ops authorized HTTP API
  -> /vehicle/teleop_command
  -> vehicle_teleop_gateway
  -> /mobile_teleop/cmd_vel
  -> vla_control_mux
  -> /control/cmd_vel_selected
  -> vla_safety_guard
  -> /cmd_vel
  -> chassis driver
```

`vehicle_teleop_gateway` 负责控制租约、控制器身份、递增序列号、Deadman、命令有效期和速度上限。`vla_control_mux` 只有在 `MOBILE_TELEOP` 模式且存在有效本地遥控租约时才选择手机候选命令。

## 3. 安全机制

- 所有写请求必须携带 Operator Token；
- 一个时刻只允许一个控制器持有遥控租约；
- 租约默认 30 秒，页面在到期前自动续租；
- 摇杆按下时 `deadman=true`，松手立即发送零命令；
- 超过 300 ms 没有新命令时，Gateway 自动输出零速；
- 页面进入后台或关闭时停止 Deadman 并尝试释放租约；
- 释放或租约过期后 Supervisor 自动回到 `MANUAL`；
- 速度上限同时受到页面设置、Gateway 上限和 Safety Guard 上限约束；
- 紧急停车调用 `/vehicle/request_safe_stop`，不绕过 Supervisor；
- 普通安全模式对前方障碍执行方向性拦截：禁止继续前进，但允许受限倒车脱困；
- 完全手动模式仅旁路激光雷达障碍物判断，急停、限速、租约、Deadman 和命令超时仍然有效；
- 正式数据采集期间强制使用普通安全模式，不能同时启用完全手动模式。

## 4. 使用顺序

1. 在组件控制中确认前视相机、车辆运行时核心、Observation 和 Shadow 数据组件正常；
2. 在手机页面设置 Operator Token；
3. 需要实际运动时单独启动“车辆底盘”；
4. 填写任务描述和操作员；
5. 点击“开始采集”；
6. 勾选车辆周围安全确认；
7. 点击“申请控制权”；
8. 按住摇杆驾驶，松手即停车；
9. 完成任务后点击“完成并保存”；失败样本可点击“丢弃本次”；
10. 点击“释放控制权”。

首次实车使用应将车辆架空或置于封闭空旷区域，并使用低速上限验证方向符号。

## 5. 训练数据

Episode Recorder 同时记录：

```text
/episode/camera/image_compressed
/odom
/imu/data_raw
/mobile_teleop/cmd_vel
/control/cmd_vel_selected
/cmd_vel
/chitu/action/target
/chitu/action/executed
/vla/task
```

其中：

- `/mobile_teleop/cmd_vel`：手机通过租约验证后的候选控制；
- `/control/cmd_vel_selected`：Mux 选择的目标命令；
- `/cmd_vel`：Safety Guard 处理后的最终执行命令；
- `/chitu/action/target`：按当前 Mobility Plugin 编码的训练目标；
- `/chitu/action/executed`：最终实际执行 Action，数据集导出优先使用该字段；
- `safety_mode`：记录 `normal` 或 `manual_obstacle_override`；
- `obstacle_override`：明确标记该 Action 是否旁路了障碍物判断；
- `safety_intervened/safety_reasons`：记录安全层是否修改 Action 以及对应规则。

这样既可以训练实际执行动作，也能审计 Safety Guard 是否对人工指令进行了修改。

## 6. ROS 接口

| 接口 | 类型 | 用途 |
|---|---|---|
| `/vehicle/acquire_control_lease` | `AcquireControlLease` Action | 获取或续期控制租约 |
| `/vehicle/release_control_lease` | `ReleaseControlLease` Service | 主动释放租约 |
| `/vehicle/control_lease` | `ControlLease` | 当前租约状态 |
| `/vehicle/teleop_command` | `TeleopCommand` | 标准化手机控制意图 |
| `/mobile_teleop/cmd_vel` | `TwistStamped` | 进入 Mux 的手机候选命令 |

## 7. HTTP API

| 方法 | 路径 | Token | 用途 |
|---|---|---|---|
| GET | `/api/teleop/status` | 否 | 查询租约、模式、Episode 和最终速度 |
| POST | `/api/teleop/acquire` | 是 | 获取或续期手机控制租约 |
| POST | `/api/teleop/command` | 是 | 发送 Deadman 和标准化摇杆输入 |
| POST | `/api/teleop/release` | 是 | 释放租约并回到 MANUAL |

## 8. 故障排查

```bash
ros2 node list | grep vehicle_teleop_gateway
ros2 action list | grep acquire_control_lease
ros2 service list | grep release_control_lease
ros2 topic info /mobile_teleop/cmd_vel -v
ros2 topic echo /vehicle/control_lease --once
ros2 topic echo /control/cmd_vel_selected --once
ros2 topic echo /cmd_vel --once
```

若手机断网，车辆应在 300 ms 左右停止；若未停止，不得继续实车测试，应先检查 Gateway、Mux 和 Safety Guard 是否都在运行。