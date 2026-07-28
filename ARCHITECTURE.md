# VLA 阿克曼小车智能平台总体设计

| 项目 | 内容 |
|---|---|
| 文档版本 | 0.2 |
| 日期 | 2026-07-28 |
| 目标车辆 | Jetson Orin NX 16GB 阿克曼小车 |
| 当前主机 | `wheeltec@10.101.70.232` |
| 当前系统 | Ubuntu 22.04、ROS 2 Humble、Nav2 |
| 首个策略实现 | SmolVLA |
| 文档状态 | 总体架构设计，待接口详细设计 |

## 1. 文档目标

本文定义阿克曼小车从现有 ROS 2 导航系统演进为 VLA 智能车辆平台的总体架构，覆盖：

- 将现有 Nav2、底盘、雷达、相机和 `rhzd_assist` 的行为与接口作为兼容参考，但不修改旧工程代码。
- 首期部署 SmolVLA，但系统不绑定任何具体 VLA。
- 支持本地推理、远程 GPU、其他 VLA 和自研策略的替换。
- 支持车端用户界面、手机 App、云端控制台和工程运维界面。
- 支持手机临时接管、控制权租约、安全仲裁和断网停车。
- 支持数据采集、训练、评估、模型发布、回滚和多车运营。
- 保持模块低耦合、接口版本化、能力可发现、组件可插拔。

本文不直接规定具体视觉模型训练参数和每个页面的最终视觉稿，这些内容在接口详细设计、UI 原型和训练方案中进一步定义。

## 2. 当前系统基线

### 2.1 车端环境

已确认车辆环境：

| 项目 | 当前状态 |
|---|---|
| 计算平台 | NVIDIA Jetson Orin NX 16GB Super |
| 操作系统 | Ubuntu 22.04.5 |
| L4T | 36.4.3 |
| CUDA | 12.6 |
| 功耗模式 | `MAXN_SUPER` |
| ROS 2 | Humble |
| RMW | CycloneDDS |
| Python | 3.10.12 |
| PyTorch | 宿主机未安装 |
| Docker | 28.5.1 |
| NVIDIA Container Runtime | 已安装 |
| 磁盘 | NVMe 约 125GB 可用 |
| 相机 | USB 前视相机，支持 MJPEG |

### 2.2 现有导航与底盘链路

```text
导航目标
  ↓
Smac Hybrid-A* 全局规划
  ↓
Nav2 MPPI Controller（Ackermann）
  ↓
/cmd_vel：geometry_msgs/msg/Twist
  ↓
wheeltec_robot_node
  ↓
/dev/wheeltec_controller
  ↓
STM32 与阿克曼底盘
```

当前已知参数：

- Nav2 控制频率：20Hz。
- 最大前进速度：0.5m/s。
- 最大后退速度：-0.5m/s。
- MPPI 运动模型：Ackermann。
- 配置最小转弯半径：2.0m。
- 底盘控制入口：`/cmd_vel`。
- 融合里程计：`/odom_combined`。
- 激光雷达：`/scan`。

### 2.3 `rhzd_assist` 当前职责

当前 `rhzd_assist` 同时承担：

- 地图、相机、路径、雷达和车体轮廓显示。
- AMCL 初始位姿设置。
- 导航目标下发。
- 规划、导航和跟随模式切换。
- 电池与状态消息显示。
- 常用坐标管理。
- 动态按钮执行 Shell 命令。

新平台将参考这些功能重新设计独立工程运维工具。原 `rhzd_assist` 代码保持只读，不作为新工具的代码基座；确需参考的交互、算法或资源，应在新工程目录中重新实现或以明确依赖方式引入。

### 2.4 Legacy 与新平台边界

现有 `/home/wheeltec/wheeltec_ros2` 和 `/home/wheeltec/workspace_rhzd` 作为 Legacy 基线保留：

- 不直接修改原 `rhzd_assist`、底盘节点、现有路径规划业务代码和原 Launch 文件。
- 不在旧工作区内新增 VLA、云端、手机 App 或新 UI 代码。
- 新平台通过标准 ROS Topic、Service、Action、TF 和参数接口集成旧系统。
- 如果旧接口不足，优先在新工程中增加 Adapter、Wrapper、Mux 或兼容 Bridge，而不是修改旧节点。
- Nav2 使用发行版或上游开源包，在新工程的 Launch 和参数中集成，不维护 Nav2 源码私有分叉。
- 旧系统可继续独立启动，新平台必须支持关闭后回到原有运行方式。

### 2.5 底盘驱动引入策略

`wheeltec_robot_node` 是基于 `rclcpp` 和串口库实现的 C++ 节点，通过 `/dev/wheeltec_controller` 与 STM32 通信，可以引入新工程。

首期建议将 `turn_on_wheeltec_robot` 和必需的 `wheeltec_robot_msg` 作为固定版本第三方源码快照放入新工程 `third_party`，记录来源仓库和提交 `21def27f9a00cc2ec58914572ac34a093c495578`。原工作区源码保持不变。

引入前需要确认源码使用与分发权限，因为当前两个包的 `package.xml` 均仍为 `TODO: License declaration`，仓库中未发现独立许可证文件。

不得同时 source 旧安装空间和新工作区中同名包，否则可能产生 ROS 包覆盖歧义。运行新平台时应明确选择新工程内 Vendor 驱动；运行 Legacy 系统时使用旧安装空间。

## 3. 总体设计原则

1. **AI 不拥有最终执行权**：VLA 只生成动作或任务建议，最终动作必须经过宿主机安全链路。
2. **实时控制留在宿主机**：20Hz 动作执行、安全过滤、控制仲裁和底盘驱动不进入 VLA 容器。
3. **SmolVLA 只是首个 Provider**：核心代码、ROS 包和数据主格式不出现具体模型绑定。
4. **统一接入，分离执行**：所有 UI 统一访问 Vehicle API，但高频遥控与最终控制使用独立实时模块。
5. **接口优先于实现**：模块通过版本化 Command、Event、Observation 和 Action 契约连接。
6. **能力协商**：加载模型或插件前先检查相机、状态字段、动作空间、任务和资源是否兼容。
7. **故障默认安全**：超时、断网、乱序、过期动作、容器退出和传感器异常默认减速停车。
8. **边缘自治**：云端不可用时，本地导航、VLA、安全、Web UI 和新 Ops Console 继续工作。
9. **权限服务端强制执行**：UI 隐藏按钮只用于体验，不能作为安全控制。
10. **数据与模型可追踪**：动作可关联车辆、任务、Episode、模型、数据集、软件和配置版本。
11. **低耦合不等于无限拆进程**：实时域独立隔离，管理域采用模块化组合，避免 Jetson 上进程膨胀。
12. **可回放与可模拟**：没有实车、真实模型或云端时，也能够通过 Mock 和 Rosbag 验证各模块。
13. **Greenfield 与 Legacy 隔离**：新平台在全新工程目录开发，旧代码只读保留，只通过公开接口或新建 Adapter 集成。
14. **ROS 2 节点统一 C++**：所有新建 ROS 2 运行节点使用 `rclcpp` 和 `ament_cmake`，不使用 `rclpy` 或 Python 编写 ROS 节点。

## 4. 总体逻辑架构

系统采用七个逻辑区域：交互入口、统一接入、领域核心、实时控制、Policy Runtime、边云平台和硬件安全。

```text
Web、App、vehicle_ops_console、云端
        ↓
Vehicle Access Gateway
        ↓
Vehicle Supervisor 与领域核心
        ↓
ROS 2 实时控制与安全 → STM32 与底盘
        ↕
Policy Runtime Docker

Cloud Agent ↔ 云端平台
```

UI 和云端不得越过领域核心直接操作实时控制；Policy Runtime 不得直接发布最终 `/cmd_vel`。

## 5. 分层与部署边界

### 5.1 硬件安全层

包含物理急停、STM32 命令看门狗、电机输出限制和硬件遥控，不依赖 ROS、Docker、网络和 VLA。

### 5.2 ROS 2 实时控制域

运行在 Jetson 宿主机，负责固定周期执行、动作时效检查、限速、障碍停车、控制仲裁和唯一 `/cmd_vel` 出口。UI、云端或模型退出时，该域仍必须完成安全停车。

### 5.3 车辆管理域

负责模式、任务、策略、模型、Episode、控制租约和运维操作，不进入 20Hz 实时链路。建议模块化组合，避免每个领域类都创建独立进程。

### 5.4 Policy Runtime

运行在 NVIDIA Docker 中，封装 PyTorch、LeRobot、SmolVLA 和其他 VLA。容器只接收 Observation 并返回 Policy Action，不直接访问 `/cmd_vel`、串口和控制权。

### 5.5 表现与云端

Web/PWA、手机 App、云端 UI、新 `vehicle_ops_console` 和第三方系统共享 Vehicle API。旧 `rhzd_assist` 可继续独立使用，但不作为新平台依赖。云端通过 `cloud_agent` 主动出站连接车辆，不直接暴露 ROS 2 DDS。

### 5.6 Legacy Adapter 层

新平台通过独立 Adapter 对接现有底盘、传感器和业务话题。底盘节点继续使用原 `/cmd_vel`、`/odom`、IMU 和串口逻辑；新 Control Mux 在外部生成最终兼容命令。Nav2 的候选输出通过新 Launch 的 remapping 接入，不修改 Nav2 或旧底盘源码。

## 6. 核心模块

### 6.1 `vehicle_supervisor`

管理人工、Nav2、VLA Shadow、VLA Assisted、VLA Autonomous、SafeStop 和 Fault 模式；校验任务、模型切换和接管请求；向所有界面输出权威状态。它不进入实时动作链。

### 6.2 `interaction_orchestrator`

将任务卡片、地图点选、文本、语音和云端指令转换为结构化 `MissionRequest`，负责歧义检查和高风险确认，不直接产生底盘动作。

### 6.3 `mission_core`

维护任务生命周期，通过插件选择 Nav2、VLA、混合、遥控、充电、巡检或泊车执行器。

### 6.4 `policy_core`

负责策略选择、能力协商、Episode 生命周期和 Policy 健康管理，不依赖具体 VLA。

### 6.5 `control_lease_manager`

负责手机 App、远程专家和其他临时控制来源的租约发放、续租、撤销、过期、抢占和审计。

### 6.6 数据、模型与运维

`episode_core` 管理通用数据采集，`model_core` 管理模型 A/B 发布回滚，`operation_core` 管理新平台的白名单运维操作。原 `rhzd_assist` 保持不变，新 Ops Console 不复用其 Shell 执行实现。

## 7. 可插拔架构

核心模块只依赖抽象接口，具体实现通过 Adapter 或 Plugin 接入。

| 插件接口 | 首期实现 | 可替换实现 |
|---|---|---|
| `PolicyProvider` | SmolVLA | 其他 VLA、自研、远程 GPU、Mock、Replay |
| `ObservationAdapter` | 前视相机加车辆状态 | 多相机、历史帧、导航上下文 |
| `ActionAdapter` | Twist Chunk | Ackermann、轨迹、局部目标、离散技能 |
| `MissionExecutor` | Nav2、VLA、Hybrid | 充电、泊车、巡检、跟随 |
| `SafetyRule` | 限速、障碍、超时 | 地理围栏、远程延迟、振荡检测 |
| `DatasetExporter` | LeRobot | Rosbag、Parquet、其他训练格式 |
| `CloudTransport` | gRPC 或 MQTT | WebSocket、Zenoh、离线模式 |
| `VideoTransport` | WebRTC | RTSP、本地录像、禁用视频 |

插件必须携带 Manifest，声明协议版本、能力、资源需求、支持任务、Observation Schema、Action Schema 和安全上限。插件不兼容时必须拒绝加载。

### 7.1 依赖规则

- 核心模块不得引用 `smolvla`、特定云厂商或具体 UI 类名。
- UI 不得调用 Docker、Nav2、Policy Provider 或底盘实现。
- Provider 不得访问控制权、急停和最终 `/cmd_vel`。
- 数据主存储不得以 LeRobot 作为唯一格式。
- 云端 Transport 不得直接发布 ROS 控制话题。

### 7.2 ROS 2 C++ 技术约束

- 所有项目自研 ROS 2 节点使用 C++17、`rclcpp`、`ament_cmake` 和 `colcon`。
- 禁止使用 `rclpy` 或 Python 实现 ROS 节点、控制节点、传感器适配器和 Vehicle API Bridge。
- ROS 接口通过 `.msg`、`.srv`、`.action` 和 `rosidl` 定义，节点间不共享内部类或全局变量。
- 参数使用 YAML，项目自有 Launch 优先使用 XML；引用 Nav2 等上游 Launch 不等于引入 Python ROS 节点。
- 长生命周期节点优先采用 Lifecycle、明确状态机和健康检查；实时链路避免阻塞调用和动态加载大模型。
- `vla_policy_gateway` 使用 C++ gRPC/Protobuf 客户端与 Policy Runtime 通信。
- Policy Runtime 可使用 Python 实现模型加载和推理，但它不是 ROS 节点，不链接 `rclpy`，只暴露版本化网络接口。
- Web UI、Cloud Agent 和训练工具可按领域选择 TypeScript、Go、Rust 或 Python，但不得绕过 C++ ROS 接入层。

## 8. 统一 Policy 接口

Policy Runtime 对外提供版本化接口：

```text
GetCapabilities
GetModelInfo
HealthCheck
StartEpisode
Predict
ResetEpisode
EndEpisode
LoadModel
UnloadModel
```

`GetCapabilities` 至少返回：

```yaml
protocol_version: 1
provider: smolvla
model_id: smolvla-corridor-v1
required_cameras: [front]
required_state: [linear_velocity, angular_velocity]
supported_actions: [twist_chunk]
supported_tasks: [corridor_following, visual_stop]
action_horizon: 16
control_period_ms: 50
supports_async: true
```

`vla_policy_gateway` 启动和切换模型时必须先完成能力协商。缺少必需相机、状态字段、动作适配器或安全能力时，模型只能保持 Disabled，不能进入 Shadow 或控制模式。

### 8.1 Observation 契约

通用 `ObservationEnvelope` 包含 `schema_version`、车辆、Episode、请求 ID、采集时间、任务、图像、车辆状态、导航上下文和控制上下文。Provider Adapter 负责转换为具体模型输入。

### 8.2 Policy Action 契约

统一动作支持联合类型：`TwistChunk`、`AckermannChunk`、`LocalTrajectory`、`LocalGoal`、`DiscreteSkill` 和 `BehaviorCommand`。首版使用 `[linear.x, angular.z]` 的 `TwistChunk`。

所有响应必须携带 `request_id`、`model_id`、`schema_version`、生成时间、有效期、动作周期和动作序列。乱序或过期响应直接丢弃。

## 9. 实时控制与安全

### 9.1 动作链

```text
Policy Action
  ↓ Action Adapter
标准动作队列
  ↓ Action Executor，20Hz
VLA 动作候选
  ↓ Control Mux，与 Nav2、手机和硬件遥控候选仲裁
/control/cmd_vel_selected
  ↓ Safety Guard，统一限速、超时和障碍停车
/cmd_vel
  ↓ wheeltec_robot_node
```

Policy 推理频率可低于 20Hz，由动作块异步填充队列。队列耗尽、响应超时或容器断开时，执行器必须受控减速停车，不能无限重复最后动作。

### 9.2 Safety Guard

安全规则包括：

- 最大前进、后退速度和角速度。
- 加速度、减速度和转向变化率。
- 雷达前后安全区和制动距离。
- 图像、里程计和 Policy 时间戳检查。
- NaN、Inf、维度、范围和控制振荡检查。
- 地理围栏、定位健康和远程延迟检查。

安全规则对 Nav2、VLA 和手机接管均有效。规则可以插件化扩展，但基础停车、限速和超时规则不得卸载。

### 9.3 控制权优先级

物理急停和硬件看门狗位于所有软件控制之上。软件控制来源建议按以下优先级：

1. 本地硬件遥控。
2. 本地手机临时接管。
3. 经本地授权的远程专家接管。
4. Nav2。
5. VLA。

`vla_control_mux` 是唯一候选控制仲裁者，输出 `/control/cmd_vel_selected`；`vla_safety_guard` 是唯一最终 `/cmd_vel` 发布者。Nav2、VLA 和遥控分别发布候选话题，禁止通过“最后发布者获胜”争抢底盘。这样安全规则天然覆盖全部软件控制来源，Mux 或任一上游插件均不能绕过最终安全出口。

## 10. UI 与统一接入

### 10.1 界面定位

| 界面 | 用户 | 主要能力 |
|---|---|---|
| 车端 Web/PWA | 普通用户、现场操作员 | 任务卡片、语言、视频、简单地图、任务进度、停止 |
| 手机 App | 现场操作员、授权专家 | 状态、任务、临时接管、视频 |
| 云端 Fleet Console | 运营、调度、管理员 | 多车、告警、任务、模型、数据、报表 |
| `vehicle_ops_console` | 研发、售后、现场工程师 | ROS、Nav2、雷达、TF、模型、日志和运维诊断 |

车端 Web 和手机 App 建议复用一套响应式前端代码，根据后端返回的 Capability 显示不同页面和操作范围。新 `vehicle_ops_console` 可选择 Qt 或 Web 技术，但必须从新工程实现，并通过统一 Vehicle API 执行危险操作。旧 `rhzd_assist` 只作为功能参考和 Legacy 调试工具保留。

### 10.2 Vehicle Access Gateway

所有 UI 逻辑上接入同一个 Gateway，Gateway 负责身份、权限、命令路由、限流、审计和接口版本。高频遥控使用独立 `mobile_teleop_gateway`，避免普通 HTTP、鉴权或 UI 阻塞进入实时链路。

权限采用四层模型：

- RBAC：viewer、user、operator、engineer、administrator、remote_expert。
- Scope：监控、任务、停车、接管、模型、数据和运维权限。
- ABAC：车辆区域、连接方式、网络质量、传感器状态和本地授权。
- Control Lease：获得底盘控制权所需的短期租约。

UI 隐藏按钮不构成安全控制，所有请求必须由 Gateway、Supervisor 和 Lease Manager 再次验证。

### 10.3 用户交互

普通用户界面围绕“车辆是否正常、谁在控制、正在做什么、进度如何、用户能做什么”组织。自然语言先转换为结构化 `MissionRequest`，对模糊或高风险任务进行确认，不将原始文本直接连接到底盘动作。

## 11. 手机 App 临时接管

### 11.1 控制租约

手机拥有操作权限并不代表自动拥有控制权。App 必须请求临时 Control Lease，租约至少包含：

```text
lease_id
vehicle_id
controller_id
issued_at
expires_at
max_speed
max_angular_velocity
allowed_area
heartbeat_interval
local_or_remote
```

Supervisor 检查用户角色、车辆位置、当前速度、传感器健康、本地授权、网络质量和已有控制者后决定是否发放。

### 11.2 遥控数据

手机发送带租约、序号、时间戳、短有效期和 Deadman 状态的归一化控制命令。`mobile_teleop_gateway` 根据租约上限转换为物理速度，再经过 Safety Guard 和 Control Mux。

以下情况立即撤销租约并停车：

- 用户松开 Deadman。
- App 进入后台或屏幕锁定。
- 控制通道断开。
- 心跳或命令超时。
- 租约过期或被高优先级来源抢占。
- 雷达、安全或车辆健康条件不再满足。

接管结束后先停车并清空旧动作，不自动恢复接管前的 VLA 动作或 Nav2 任务，由用户确认是否继续。

### 11.3 本地与远程接管

本地手机优先通过局域网安全 WebSocket 连接车端。远程专家通过云端认证、视频信令和低延迟数据通道接入，必须额外启用本地授权、更低速度、地理围栏、网络质量门槛和全过程审计。

## 12. 云端监控与控制

### 12.1 Cloud Agent

车端 `cloud_agent` 主动建立出站连接，负责设备认证、遥测、告警、命令接收、命令验签与去重、模型下载、数据上传和断网缓存。Jetson 不直接向公网开放 ROS 端口。

云端命令必须包含 `command_id`、车辆、操作者、角色、签发时间、过期时间和认证上下文。过期命令不得在断网恢复后补执行。

### 12.2 云端能力等级

| 等级 | 能力 | 默认策略 |
|---|---|---|
| L0 | 只读监控、告警和报表 | 默认允许 |
| L1 | 任务下发、取消、SafeStop 请求 | 授权后允许 |
| L2 | 远程专家辅助接管 | 本地授权和专用安全条件下允许 |
| L3 | 直接底盘实时控制 | 默认禁止 |

云端优先下发任务级命令，例如前往区域、开始巡检、取消任务和切换 Nav2，而不是连续发送 `/cmd_vel`。

### 12.3 视频与数据通道

- 遥测和事件：gRPC Stream 或 MQTT。
- 任务和管理命令：HTTPS 或 gRPC。
- 实时视频：WebRTC，按需开启并自适应码率。
- Episode 与模型：对象存储式分片上传下载。

视频、遥测、控制和模型传输必须相互独立，避免视频拥塞影响控制命令。

### 12.4 断网策略

断网后 Nav2、本地 VLA、Safety、车端 Web、手机本地接管、新 Ops Console 和数据采集继续工作。Cloud Agent 缓存遥测、告警和 Episode，恢复后补传；控制命令超过有效期后丢弃。

## 13. 新工程运维工具

新建 `vehicle_ops_console` 作为本地工程运维与诊断工具。旧 `rhzd_assist` 不修改、不关闭、不作为新工具代码基座，可继续用于 Legacy 系统调试，并作为功能、交互和显示逻辑的参考。

新 Ops Console 计划实现：

- 地图、AMCL、路径、Costmap、雷达、TF 和相机显示。
- ROS 节点、话题、QoS、频率和底盘串口诊断。
- Provider、模型、Observation、Action Chunk 和推理延迟诊断。
- Policy 原始动作、安全过滤动作和最终执行动作对比。
- Shadow 模式、Episode 质量、模型预热和 A/B 回滚状态。
- 日志、资源、云端连接和诊断包收集。

新工具禁止采用：

- 任意 Shell 命令按钮。
- UI 内直接管理 Docker 和进程。
- UI 自己判断权威模式。
- UI 直接提升自动控制权限。
- 普通用户任务交互和云端业务逻辑。

所有危险操作调用 `operation_core` 的白名单 `operation_id`，后台检查角色、车辆是否运动、是否需要 SafeStop、确认要求和审计信息。

如果需要参考旧代码中的地图坐标换算、图层显示或 Qt 交互，应在新工程目录中重新实现或提取为经过审查的独立库，不在原仓库直接修改。

## 14. 状态机

车辆主要模式：

```text
MANUAL
NAV2
VLA_SHADOW
VLA_ASSISTED
VLA_AUTONOMOUS
MOBILE_TELEOP
REMOTE_TELEOP
SAFE_STOP
FAULT
```

模型、UI、容器或云端不得自行从低权限模式进入高权限模式。容器重启、模型切换或故障恢复后默认回到 `VLA_SHADOW` 或 `MANUAL`。

## 15. 数据与训练闭环

### 15.1 通用 Episode

原始数据采用模型无关格式，至少保存：

```text
图像与传感器原始数据
任务文本与结构化任务
operator_action
policy_action
safe_action
executed_action
人工接管和 Safety 事件
任务结果与备注
相机、车辆、软件和模型版本
```

通过 `DatasetExporter` 转换为 LeRobot、Rosbag、Parquet 或其他训练格式。LeRobot 是首期训练出口，不是唯一主存储。

### 15.2 训练与发布流程

```text
车端采集
  → 数据质量检查
  → 通用 Episode 版本
  → 训练格式导出
  → 外部 GPU 微调
  → 离线评估
  → Rosbag 回放
  → 实车 Shadow
  → 封闭场地低速闭环
  → 模型发布
  → 运行监控与失败片段回流
```

### 15.3 模型 Manifest

每个模型制品记录 Provider、基础模型、数据集版本、代码提交、Runtime 镜像、Observation Schema、Action Schema、支持任务、传感器要求、速度限制、评估报告和回滚版本。

车端使用 A/B 模型槽。更新先下载到非活动槽，完成校验、预热和 Shadow 验证后再原子切换；失败立即回滚，不删除当前可用模型。

## 16. Command、Event 与版本化契约

跨模块命令包括开始任务、取消任务、请求模式、SafeStop、开始 Episode、部署模型和执行运维操作。状态变化通过 Event 发布，例如任务进度、控制权变化、Safety 接管、模型激活和 Policy 断开。

核心契约采用版本号：

```text
vehicle.command.v1
vehicle.event.v1
vehicle.state.v1
vehicle.mission.v1
vehicle.observation.v1
vehicle.policy_action.v1
vehicle.policy_capabilities.v1
vehicle.telemetry.v1
vehicle.episode.v1
```

消息至少携带 `schema_version`、`message_id`、时间戳、来源和关联 ID。新增字段优先保持向后兼容，不得复用旧字段表达新语义。

## 17. 建议部署单元

### 17.1 Greenfield 工程根目录

新平台全部放在独立根目录，不向旧工作区写入文件：

```text
/home/wheeltec/vla_vehicle_platform/
├── ros_ws/src/
│   ├── vehicle_interfaces
│   ├── vehicle_manager
│   ├── vehicle_adapters
│   ├── vla_policy_gateway
│   ├── vla_action_runtime
│   ├── vla_safety_guard
│   ├── vla_control_mux
│   ├── vla_episode_recorder
│   ├── mobile_teleop_gateway
│   ├── vehicle_access_gateway
│   ├── vehicle_ops_console
│   └── vehicle_bringup
├── third_party/
│   └── wheeltec_ros2/
│       ├── turn_on_wheeltec_robot
│       └── wheeltec_robot_msg
├── patches/
│   └── wheeltec_ros2
├── policy-runtime
├── edge-ui
├── cloud-agent
├── models
├── datasets
├── cache
├── config
└── logs
```

新平台模式只 source ROS 发行版和新工作区：

```bash
source /opt/ros/humble/setup.bash
source /home/wheeltec/vla_vehicle_platform/ros_ws/install/setup.bash
```

新平台模式不得同时 source 旧 `wheeltec_ros2/install` 或 `workspace_rhzd/install`，避免同名包和环境链式覆盖。Legacy 模式继续使用原有 source 与启动方式，两种模式通过独立启动脚本明确选择。

### 17.2 Nav2 与底盘集成

- Nav2 使用固定版本的 ROS 2 Humble 上游源码快照并纳入新工程依赖闭环，不维护业务私有分叉。
- 新平台在自己的 `vehicle_bringup` 中维护 Nav2 参数、Launch、remapping 和候选控制输出。
- 不修改 Nav2 上游源码，也不要求修改旧 `rhzd_path_planner`。
- 将固定版本的 `wheeltec_robot_node` 源码快照引入新工程并独立构建，保持串口协议和运行行为兼容；原仓库保持不变。
- 新 `ChassisAdapter` 只使用 `/cmd_vel`、里程计、IMU、电量和公开状态接口。
- Vendor 底盘包中未使用的 `rclpy` 和 `ackermann_msgs` 依赖由新工程副本清理；供应商自带 Python Launch 保留为上游资料但不作为新平台启动入口，新平台自有 Launch 使用 XML 与 YAML。
- 如需新增仲裁、时间戳或安全语义，在新平台增加 Wrapper、Mux 或 Adapter，不向原底盘仓库写入代码。

### 17.3 进程与容器

- 实时控制进程：Action Executor、Safety Guard、Control Mux。
- 车辆管理进程：Supervisor、Mission、Policy、Episode、Model、Operation Core。
- 适配进程：Observation、Nav2、传感器健康和底盘适配。
- Policy Runtime 容器：Provider Router 与 VLA Provider。
- Edge UI 容器：Web/PWA 静态资源和本地 API 代理。
- Cloud Agent：可作为独立服务或容器，故障不影响车端自治。

### 17.4 源码依赖闭环与 Phase 0 状态

新工程在 `third_party/wheeltec_ros2` 中保存 Nav2 Humble、`serial_ros2`、`wheeltec_robot_msg` 和 `turn_on_wheeltec_robot` 源码快照。`scripts/prepare_workspace.sh` 将这些包链接到新 `ros_ws/src/vendor`，构建脚本只 source `/opt/ros/humble/setup.bash`，不继承两个 Legacy 工作区的 `install` 环境。标准 ROS、Ubuntu、CUDA 和 JetPack 库仍作为固定版本系统依赖，不复制旧工作区生成物。

截至 2026-07-28，Phase 0 已完成并在 Jetson 车端隔离验证：

- `vehicle_interfaces`、`vehicle_runtime`、`vehicle_bringup` 构建通过。
- Supervisor、Mock Policy、Action Runtime、Control Mux、Safety Guard 均为 C++17 `rclcpp` 节点。
- 使用 `ROS_DOMAIN_ID=42` 验证 Shadow 模式、零速策略链、最终零速 `/cmd_vel` 和 SafeStop 状态。
- Phase 0 Launch 不启动 `wheeltec_robot_node`，不访问 STM32 串口，不产生车辆运动。
- 第三方底盘源码许可证尚未明确，外部分发前必须完成供应商授权审查。

## 18. 关键 ROS 接口建议

候选话题：

```text
/nav2/cmd_vel
/vla/cmd_vel_raw
/mobile_teleop/cmd_vel
/hardware_teleop/cmd_vel
/control/cmd_vel_selected
/cmd_vel
/vehicle/system_state
/vehicle/control_state
/mission/state
/vla/policy_state
/vla/safety_state
/control/lease_state
/system/event
```

模型动作优先使用带时间戳的 `TwistStamped` 或自定义版本化消息，进入现有底盘前再转换为 `Twist`。

## 19. 故障与降级策略

| 故障 | 系统响应 |
|---|---|
| Policy Service 无响应 | 清空队列并减速停车，可切回 Nav2 或人工 |
| 动作过期或乱序 | 丢弃整个响应并记录事件 |
| 相机或里程计过旧 | 禁止新推理并进入 SafeStop |
| 雷达安全区触发 | 覆盖所有软件控制来源 |
| 手机断网或 App 后台 | 撤销租约并停车 |
| 云端断开 | 保持本地自治，缓存数据和告警 |
| UI 崩溃 | 控制链继续运行，任务状态保持在 Supervisor |
| 容器重启 | 不自动恢复控制，回到 Shadow 或 Manual |
| 模型更新失败 | 保留活动槽并回滚 |
| Supervisor 退出 | 实时控制域进入 SafeStop 或人工模式 |

软件“急停”只能请求 SafeStop，不能替代物理急停和 STM32 看门狗。

## 20. 可观测性与审计

至少记录：

- 相机采集到 Policy 请求的延迟。
- Policy 排队、预处理和推理延迟。
- Action Chunk 年龄、剩余长度和丢弃原因。
- 20Hz 执行周期抖动。
- Safety 限幅、停车和接管次数。
- 当前控制来源和租约状态。
- CPU、GPU、内存、温度、功耗和磁盘。
- 任务成功、失败、取消和人工接管率。
- 云端连接、数据上传和模型发布状态。

日志统一使用 `vehicle_id`、`boot_id`、`mission_id`、`episode_id`、`request_id`、`model_id`、`operator_id` 和 `command_id` 关联。

所有高风险操作必须审计：操作者、来源、角色、请求时间、确认信息、车辆状态、执行结果和失败原因。

## 21. 测试策略

每类插件提供 Mock 或 Replay 实现：

```text
MockPolicyProvider
ReplayPolicyProvider
MockMissionExecutor
MockCloudTransport
MockChassisAdapter
ReplayObservationSource
FaultInjectionSafetyRule
```

测试覆盖契约兼容、超时、断连、乱序、重复命令、资源不足、模型不兼容、手机断网、云端断网和故障注入。通过 Rosbag 可在无实车条件下测试 UI、Policy、数据和任务链路。

## 22. 分阶段实施路线

### 阶段 0：平台骨架

- 创建 `vla_ros_ws` 和 Policy Runtime 工程。
- 定义 Command、Event、Observation、Action 和 Capability 契约。
- 实现 Mock Provider、Supervisor 基础状态机和健康检查。
- 验证容器内 CUDA/PyTorch，不连接底盘控制。

退出条件：没有真实 VLA 时也能跑通 UI、Gateway、Mock Policy 和状态链路。

### 阶段 1：数据与 Shadow

- 启动和标定前视相机。
- 实现 Episode Recorder 和通用数据格式。
- 接入 SmolVLA Provider。
- 完成外部 GPU 微调、Rosbag 回放和实车 Shadow。

退出条件：模型不控制车辆，延迟、动作质量和数据完整性达到门槛。

### 阶段 2：低速闭环

- 接入 Action Executor、Safety Guard 和 Control Mux。
- Nav2 输出重映射到候选话题。
- 首期限速 `0.2m/s`，仅在封闭区域和任务白名单内启用。
- 验证急停、超时、障碍停车、人工接管和回滚。

### 阶段 3：新用户界面与手机 App

- 建立 Vehicle API、Web/PWA 和 Capability 驱动界面。
- 实现任务卡片、语言输入、视频、简单地图和进度展示。
- 实现本地手机 Control Lease、Deadman 和断线停车。
- 新建 `vehicle_ops_console`，参考旧 `rhzd_assist` 功能但不修改或复用其工程代码。

### 阶段 4：云端监控与任务

- 上线 Cloud Agent、设备身份、遥测、告警和 Store-and-Forward。
- 先开放只读监控，再开放任务级控制。
- 建立模型注册、数据上传、灰度发布和审计。

### 阶段 5：远程专家与多车

- 增加 WebRTC 视频和受限远程接管。
- 增加多车调度、失败片段分析和模型分组发布。
- 接入第二个 Policy Provider，验证 VLA 可替换性。

### 阶段 6：分层自主系统

高层 VLA 负责语言、场景、任务拆分和行为选择；Nav2 或专用低层策略负责局部规划和运动控制。保持高层语义与低层安全控制分离。

## 23. 首版技术决策

| 决策项 | 首版选择 | 演进方向 |
|---|---|---|
| ROS 集成 | 宿主机 `rclcpp` | 可组合组件或独立进程 |
| ROS 节点语言 | C++17，禁止 `rclpy` | 后续可升级 C++20 |
| 项目 Launch | XML 加 YAML 参数 | 可包含上游 Nav2 Launch |
| 底盘驱动 | 固定提交 Vendor 源码 | 独立重构 `wheeltec_chassis_driver` |
| Policy 通信 | localhost gRPC | Unix Socket、共享内存、远程 GPU |
| AI 部署 | 单个 NVIDIA Docker | 多模型 Router |
| 首个 Provider | SmolVLA | 其他 VLA 和自研策略 |
| 动作格式 | Twist Chunk | Ackermann、轨迹、局部目标 |
| 执行频率 | 宿主机 20Hz | 按底盘周期调整 |
| UI | Web/PWA 加 Qt 运维台 | 手机壳、云端多车 UI |
| 手机控制 | 短期 Control Lease | 远程专家受限接管 |
| 云端 | 出站 Cloud Agent | 多云或私有化部署 |
| 原始数据 | 通用 Episode | 多种 Dataset Exporter |
| 最终控制权 | Control Mux 仲裁，Safety Guard 唯一输出 | 保持双层边界 |

## 24. 实现前需要冻结的参数

1. 相机分辨率、帧率、内参、外参和固定设备路径。
2. 底盘真实轴距、最大舵角、转向符号、最小转弯半径和制动距离。
3. `/cmd_vel.angular.z` 在 STM32 中的真实语义、换算和饱和范围。
4. 人工遥控输入、物理急停和底盘看门狗行为。
5. 车体 Footprint、雷达安全区和允许区域。
6. 首批任务集合、语言模板、成功判定和任务白名单。
7. Policy 请求频率、动作周期、Chunk 长度和超时阈值。
8. 手机本地和远程接管的最大速度、租约时长和网络门槛。
9. 云端部署方式、设备身份、证书、数据保留和隐私规则。
10. 训练工作站、数据仓库、模型仓库和发布审批流程。

## 25. 总体验收标准

### 架构可替换性

- Mock Provider 与 SmolVLA Provider 可在不修改 ROS 控制链的情况下切换。
- 第二个 Provider 接入时不修改 Vehicle API、Supervisor 和 Safety Guard。
- Web UI、手机 App、云端和新 `vehicle_ops_console` 共享权威状态和权限规则。
- 新 ROS 工作区不依赖任何自研 Python ROS 节点，Policy Python 环境与 ROS 完全隔离。
- 新平台模式不 source 旧同名底盘包，Legacy 模式和新平台模式可独立启动。

### 控制安全

- 只有 Safety Guard 能发布最终 `/cmd_vel`，Control Mux 只能发布仲裁后的候选命令。
- Policy、UI、手机和云端断开后车辆能在规定时间内进入安全状态。
- 手机租约、Deadman、过期和抢占均能正确停车。
- Safety Guard 能覆盖所有软件控制来源。

### 边缘自治

- 云端断网不影响本地 Nav2、VLA、UI、安全和数据采集。
- 过期云端命令不会在恢复连接后执行。
- 模型更新失败可自动保留或回滚到可用版本。

### 数据闭环

- 每个 Episode 能关联任务、模型、操作者、动作链和运行配置。
- 能从通用 Episode 导出至少一种 VLA 训练格式和 Rosbag 回放格式。
- Shadow、Safety 接管和人工纠正数据能够进入后续训练分析。

## 26. 结论

本项目的目标不是在现有导航系统旁边增加一个 SmolVLA 节点，而是建立一个以稳定任务、安全和数据契约为核心的车辆智能平台。

SmolVLA、其他 VLA、Nav2、手机 App、车端 Web、云端平台和新 Ops Console 都是可替换或可扩展的实现；旧 `rhzd_assist` 与旧底盘工程作为只读 Legacy 基线独立保留。系统长期不变的部分是：

- Vehicle Supervisor 管理权威任务和模式。
- Vehicle Access Gateway 统一界面与权限入口。
- Policy API 隔离不同 VLA。
- Action Adapter 隔离不同动作空间。
- Safety Guard 与 Control Mux 保持最终安全边界。
- 通用 Episode 支撑数据和模型持续演进。

任何可插拔组件被移除、崩溃或失联后，车辆至少应能够安全停车并回到人工可控状态。
