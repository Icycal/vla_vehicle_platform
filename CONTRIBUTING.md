# 开发约定

## 分支

- `main`：可构建、可部署的稳定主线。
- `feature/<name>`：功能开发。
- `fix/<name>`：缺陷修复。
- `release/<version>`：需要冻结验证时使用。

## 提交

建议使用简洁的 Conventional Commits 前缀：

```text
feat: add control lease manager
fix: reject expired policy chunks
docs: update phase 1 integration plan
build: pin vehicle dependencies
```

一次提交只解决一个逻辑问题。不要提交构建目录、模型权重、数据集、Rosbag、密钥或
车端运行日志。

## ROS 2 规则

- 项目自有节点使用 C++17、`rclcpp`、`ament_cmake`。
- 项目自有 Launch 优先使用 XML，参数使用 YAML。
- Policy Runtime 可以使用 Python，但不得在其中实现项目自有 ROS 节点。
- 最终 `/cmd_vel` 只能由 Safety Guard 发布。
- 不修改两个 Legacy 工作区；需要的源码只能导入本仓库后维护。

## 验证顺序

1. 运行 `scripts/build.sh` 验证项目自有 Phase 0 包。
2. 运行 `scripts/run_phase0.sh`，在隔离 ROS Domain 中验证零速链路。
3. 运行 `scripts/build_chassis.sh` 验证 Vendor 底盘源码兼容性。
4. 只有经过明确硬件测试计划后才允许启动 `wheeltec_robot_node`。
