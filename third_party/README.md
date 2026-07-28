# Third-party source policy

本目录保存新工作区构建所需的固定第三方源码快照。当前采用 vendoring，而不是 Git
submodule，原因是导入来源包含供应商单仓库子目录和本地修改过的 `serial_ros2`，无法
安全地用公开远端提交完整复现。

规则：

- 每次升级必须更新 `../DEPENDENCIES.md` 中的来源、提交、导入日期和脏状态。
- 供应商代码的兼容修改只允许发生在本目录副本中，不得回写 Legacy 工作区。
- 不提交第三方构建产物或传输归档。
- Nav2 保持上游兼容，不加入业务私有行为修改。
- 未明确许可证的 Wheeltec 源码不得对外分发。

当所有依赖都具备稳定远端、明确许可证和可复现提交后，可以逐项评估改为 Git
submodule 或 `.repos`/vcs import 锁定方式。
