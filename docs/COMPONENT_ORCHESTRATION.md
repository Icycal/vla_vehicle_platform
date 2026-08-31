# 组件编排与网页控制

## 1. 目标

`operation_orchestrator` 为 Vehicle Ops Console 提供白名单组件启动、停止、重启、状态和日志能力。浏览器不执行 Shell，不接受任意命令、路径或 systemd unit 名称。

管理面 `vla-ops-console.service` 常驻运行，不属于可停止组件。即使执行“停止受管链路”，网页与编排 API 仍保持在线。

## 2. 当前受管组件

| 组件 ID | systemd 用户服务 | 内容 | 依赖 |
|---|---|---|---|
| `front_camera` | `vla-front-camera.service` | USB 前视相机与压缩图像 | 无 |
| `vehicle_chassis` | `vla-vehicle-chassis.service` | 车辆底盘驱动、里程计与 IMU | 无 |
| `runtime_core` | `vla-runtime-core.service` | Supervisor、Teleop Gateway、Policy Gateway、Action Runtime、Mux、Safety Guard | 无 |
| `observation_pipeline` | `vla-observation-pipeline.service` | Observation Monitor 与 Adapter | 相机、运行时核心 |
| `vla_debug_pipeline` | `vla-debug-pipeline.service` | Pipeline Trace 与 VLA Debug Orchestrator | Observation |
| `shadow_data` | `vla-shadow-data.service` | Shadow Evaluator 与 Episode Recorder | VLA 调试链路 |

`vehicle_chassis` 是通用底盘管理入口。当前车辆默认通过 `vehicle_bringup/vehicle_chassis_wheeltec.launch.xml` 封装 `turn_on_wheeltec_robot` 串口节点接入 Wheeltec STM32；启动包、Launch 文件、Launch 参数和控制 Topic 均由 `run/config/chassis.env` 配置，不要求平台安装在固定目录，也不要求使用 `wheeltec` 用户。替换其他底盘时应提供等价 ROS 2 驱动，并同步调整组件注册中的健康节点和 Topic。

## 3. 一键场景

- `camera_only`：只启动前视相机。
- `vla_debug`：启动相机、运行时、Observation 和 VLA 调试链路。
- `full_shadow`：启动全部五个受管组件。

一键场景启动按依赖顺序执行，停止按反向顺序执行。组件卡片上的启动、停止和重启只控制当前组件；例如单独停止相机时，Observation、VLA 调试和 Shadow 数据链路仍保持运行，但可能因相机输入中断进入无数据或降级状态。需要整体启停时使用一键场景控制，Vehicle Ops 管理面始终保持运行。

## 4. 安装与启动

```bash
cd /home/wheeltec/vla_vehicle_platform
./scripts/build.sh
./scripts/install_component_services.sh
systemctl --user restart vla-ops-console.service
```

安装脚本会：

1. 将 unit 安装到 `~/.config/systemd/user/`；
2. 创建 `run/log/components/`，并在首次安装时生成 `run/config/chassis.env`；
3. 自动启用常驻管理面 `vla-ops-console.service`；
4. 不自动启用相机或 Shadow 组件，避免车辆启动后自动占用设备和 GPU。

## 5. 网页使用

1. 打开 `http://<vehicle-ip>:8088/#debug`；
2. 进入“组件控制 / Components”；
3. 只读状态无需令牌；
4. 启动、停止、重启、读取日志需要右上角 Operator Token；
5. 相机离线时，实时监控画面中会出现“启动前视相机”。

组件状态：

- `running`：systemd、ROS 节点和健康 Topic 均正常；
- `degraded`：服务运行，但节点或 Topic 不完整；
- `external`：同名 ROS 节点由旧 Launch 或其他进程启动，网页控制禁用；
- `failed`：服务异常退出；
- `unavailable`：unit 尚未安装。

## 6. 日志

每个组件只写入固定白名单日志：

```text
run/log/components/front_camera.log
run/log/components/vehicle_chassis.log
run/log/components/runtime_core.log
run/log/components/observation_pipeline.log
run/log/components/vla_debug_pipeline.log
run/log/components/shadow_data.log
run/log/components/ops_console.log
```

网页最多读取最后 500 行，不能传入任意日志路径。

## 7. 命令行运维

```bash
systemctl --user status vla-ops-console.service
systemctl --user status vla-front-camera.service
systemctl --user restart vla-front-camera.service
systemctl --user start vla-vehicle-chassis.service
systemctl --user stop vla-shadow-data.service
```

查看全部状态：

```bash
curl http://127.0.0.1:8088/api/components
```

## 8. 安全边界

- 所有写操作继续使用已有 Operator Token；
- 配置只接受编译进工程并随发布部署的组件白名单；
- systemctl 通过 `execvp` 参数数组调用，不经过 Shell；
- 组件日志路径由后端配置决定，浏览器不能指定路径；
- 管理面与受管业务组件分离；
- 停止底盘服务前会向配置的控制 Topic 发送一次零速命令，再释放驱动进程和硬件设备；
- 当前版本不把 Nav2、激光雷达和外部手柄作为默认受管组件，它们属于按车型与任务安装的可选能力；手机遥控 Gateway 已包含在运行时核心中。

## 9. 全链路控制项审计

当前默认组件已覆盖：前视相机、底盘驱动、运行时控制核心、Observation、VLA 单步调试、Shadow 评估和训练数据采集。Policy Runtime 的 Mock/SmolVLA 切换及模型激活由“模型管理”任务控制，不重复作为组件卡片。

仍建议后续按插件化方式补充以下可选组件，而不是默认绑定到所有 Linux 平台：

- `localization_source`：外部定位、融合里程计或 EKF；
- `lidar_driver`：二维/三维激光雷达；
- `navigation_stack`：Nav2 规划与导航；
- `manual_teleop`：外部手柄或专用遥控器接管；手机局域网遥控已经实现；
- `rear_camera` / `depth_camera`：多相机或深度传感器。

Vehicle Ops 管理面必须常驻，因此不加入可停止组件。底盘不加入 `full_shadow`，避免只做影子调试或数据检查时意外占用串口；需要车辆实际运动时由操作者单独启动“车辆底盘”。
