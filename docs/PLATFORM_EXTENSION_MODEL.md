# 平台扩展与实现解耦原则

## 1. 平台边界

车端平台只定义稳定的数据契约、控制仲裁、安全门禁、调试协议和运维接口，不把 SmolVLA、OpenVLA、LeRobot 或其他实现名称写入核心业务逻辑。

- 模型实现通过 Policy Provider 接入。
- 模型输入通过 `vehicle.observation.v1` 接入。
- 模型输出通过版本化 Policy Action 接入。
- 训练数据格式通过 Dataset Adapter 接入。
- 存储展示通过 `storage.yaml` 分类配置接入。
- 页面显示 Provider 和 Model ID 的运行时上报值，不根据模型名称分支。

## 2. 更换 VLA

更换 SmolVLA 为 OpenVLA 或其他实现时，平台侧保持以下内容不变：相机、Observation、Supervisor、Control Mux、Safety Guard、Pipeline Trace、Episode Recorder、Vehicle Ops API 和网页。

需要替换或增加的是 Provider、模型 Manifest、动作适配器、运行镜像以及对应的数据转换适配器。新的实现不得直接发布最终 `/cmd_vel`。

## 3. 配置驱动

存储分类位于 `ros_ws/src/vehicle_storage/config/storage.yaml`。新增训练格式、模型资产目录或调试工件目录时，应优先增加或修改 YAML 分类，而不是修改 C++ 和 JavaScript。

当前 `training_datasets` 默认指向已有的 `datasets/lerobot`，这只是当前安装实例的路径，不代表平台依赖 LeRobot。未来可以改为 `datasets/openvla`、`datasets/pi0` 或统一的 `datasets/training/<adapter>`。

## 4. 仍需继续解耦的实现

当前工程工具中的训练数据转换和部分模型健康检查仍由 SmolVLA/LeRobot 专用脚本实现。后续应增加 Dataset Adapter 与 Policy Tool Adapter 注册表，使 Job Center 根据配置展示适配器能力，而不是继续增加模型名称分支。
