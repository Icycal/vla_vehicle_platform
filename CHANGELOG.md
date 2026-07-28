# Changelog

本项目遵循语义化版本思路，正式发布前版本保持 `0.x`。

## 0.1.0 - 2026-07-28

### Added

- 建立绿色 ROS 2 Humble 工作区和 C++17 工程规则。
- 增加 Supervisor、Mock Policy Gateway、Action Runtime、Control Mux 和 Safety Guard。
- 增加版本化消息、服务和动作接口。
- 增加 XML Phase 0 Launch、YAML 参数和隔离构建脚本。
- 导入 Nav2 Humble、Wheeltec 底盘、消息和串口源码快照。
- 完成车端零速 `/cmd_vel`、Shadow 模式和 SafeStop 验证。

### Safety

- Phase 0 不启动底盘串口节点。
- Safety Guard 作为唯一最终 `/cmd_vel` 发布者。

### Known Issues

- Wheeltec Vendor 包许可证声明尚未明确。
- Vendor 串口和底盘代码仍有既有编译警告。
