# 主动任务调试

主动任务调试与快照单步是两种不同的调试模式：

- 快照单步：冻结一张图片，手动执行契约、预处理、推理和动作解释。
- 主动任务调试：只输入一次任务，车端持续读取实时 Observation 并观察现有 VLA Shadow 链路。

## 安全边界

当前实现固定为 Shadow-only：调用的是现有 `/vla/task` 和 `/vla/pipeline_trace` 链路，不发布真实车辆控制指令，也不改变控制仲裁结果。

## 会话接口

创建会话需要 Operator Token：

```text
POST /api/active-debug/sessions
```

请求示例：

```json
{
  "task": "沿通道前进，遇到障碍物停车",
  "mode": "shadow",
  "hz": 1.0,
  "max_duration_seconds": 60,
  "max_steps": 100
}
```

查询当前会话：

```text
GET /api/active-debug/session
```

控制会话：

```text
POST /api/active-debug/session/pause
POST /api/active-debug/session/resume
POST /api/active-debug/session/stop
```

## 停止条件

- 用户停止；
- 用户暂停后不再处理新帧；
- 超过最大运行时间；
- 达到最大步数；
- Trace 阶段报告 `FAILED` 或 `REJECTED`；
- Trace 阶段消息或输出包含统一完成信号 `success`、`completed` 或 `terminate`。

完成信号目前是过渡实现，后续应由独立的 `TaskTerminationEvaluator` 提供结构化结果，避免依赖文本匹配。

## 页面使用

打开 Vehicle Ops 的 `VLA 调试` 页面，在“实时链路”顶部的“主动调试链路”卡片输入任务，点击“开始主动调试”。页面每秒刷新会话状态，同时继续展示当前实时链路的八个阶段。

主动调试结束后查看“停止原因”和最近 Observation；需要重新运行时再次输入任务并启动新会话。
