# Vehicle Ops 存储管理

## 1. 目标

存储管理中心为车端生成数据提供统一容量监控和白名单清理能力。浏览器不能传入任意路径，只能选择 `storage.yaml` 中声明的分类和分类根目录下的直接子项。

## 2. 页面

打开 `http://10.101.70.232:8088`，进入“存储管理”。页面显示总容量、使用率、可用空间、分类占用、项目数量和详细项目。查看明细与执行清理需要 Operator Token。

清理流程固定为：选择项目、后端预览、显示预计释放空间、用户二次确认、后端重新校验保护状态、执行清理。

## 3. 分类

配置文件为 `ros_ws/src/vehicle_storage/config/storage.yaml`，生产部署可通过安装空间中的同名参数文件加载。当前分类包括 Episode、训练数据集、数据导出包、模型调试记录、工程任务、组件日志、ROS 构建日志、测试数据、训练输出和模型资产。分类 ID、显示名称、路径和清理权限全部来自 YAML 参数，不写死在网页或存储节点中。当前训练数据路径仍指向 `datasets/lerobot`，切换到 OpenVLA 或其他训练格式时只需调整配置路径，无需修改 C++ 或网页代码。

模型分类默认只读。正在录制或结束写入中的 Episode、处于 queued/running 的 Job 禁止删除。组件日志采用截断方式释放空间，不删除被 systemd 打开的日志文件。

## 4. ROS 接口

- `/vehicle/storage/get_status`：磁盘和分类容量。
- `/vehicle/storage/list_items`：固定分类下的直接子项。
- `/vehicle/storage/cleanup`：预览或执行选择性清理。

HTTP API：

- `GET /api/storage`：容量概要，无需 Token。
- `GET /api/storage/items/<category-id>`：分类明细，需要 Token。
- `POST /api/storage/cleanup`：预览或执行清理，需要 Token。

## 5. 安全边界

- 不接受绝对路径、相对路径、`..` 或包含目录分隔符的项目 ID。
- 不跟随或清理符号链接。
- 单次最多处理 100 个直接子项。
- 模型默认禁止清理。
- 清理执行前会再次检查 Episode 和 Job 的活动状态。
- 节点不发布控制 Topic，不访问 STM32，不启动底盘节点。

## 6. 当前限制

第一版只提供人工选择清理，不自动执行保留策略；Docker 镜像和 Build Cache 仍需独立受控接口，不能通过任意 `docker prune` 暴露给浏览器。后续可增加日志轮转、按天数/数量保留、归档状态和训练服务器上传确认。
