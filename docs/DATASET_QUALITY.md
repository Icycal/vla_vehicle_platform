# 数据集质量检查与 Episode 切分

## 目标

训练前必须先验证数据完整性，并按完整 Episode 切分训练、验证和测试集合。禁止按帧随机切分，因为相邻帧高度相关，会导致同一段行驶轨迹同时出现在训练集和评估集，产生数据泄漏和虚假的评估结果。

本工程保留两层数据格式：

- `vehicle.dataset.v1`：模型无关的车辆中间格式，一次导出对应一个 Episode；
- LeRobot v3：由一个或多个车辆 Episode 转换得到的 SmolVLA 训练格式。

质量工具同时支持这两种格式，切分工具只接收原始 `vehicle.dataset.v1` Episode。应先切分，再分别转换各 Split，避免先合并后无法追踪 Episode 边界。

## Dataset Inspector

检查车辆中间数据集：

```bash
./scripts/inspect_dataset.sh \
  datasets/exports/episode-001 \
  run/test/dataset-quality/episode-001
```

检查 LeRobot 数据集：

```bash
./scripts/inspect_dataset.sh \
  datasets/lerobot/ackermann-train-v1 \
  run/test/dataset-quality/ackermann-train-v1
```

脚本使用现有 `vla-lerobot-compat:0.4.3` 镜像，以 `--network none`、只读 Dataset 挂载运行，不需要 GPU。输出目录必须不存在，结果通过 `.partial` 临时目录原子生成：

```text
dataset_quality_report.json
dataset_quality_summary.md
```

JSON Schema 为 `vehicle.dataset.quality.v1`。主要检查项包括：

- Episode、帧和任务数量；
- 图像缺失、损坏、尺寸变化和高重复率；
- 每个 Episode 内的时间戳顺序、帧间隔和实测 FPS；
- 状态字段有效率和非有限数值；
- 动作范围、均值、标准差、零值比例和缺失训练目标；
- Episode 数量不足、单任务和退化动作等训练风险。

只要存在 `blocker` 或 `error`，`training_readiness` 就为 `false`。`warning` 不直接阻止训练，但必须在正式训练前人工审查。

当前 `smolvla-training-smoke` 数据只有一个 Episode，且 Ackermann 动作全部为零。它只用于验证导出、转换、加载和反向传播接口，质量报告会给出 `degenerate_actions`、`insufficient_episodes` 以及部分 `state_never_valid`，不能作为有效训练集。

## Episode Split

创建确定性 Split Manifest：

```bash
./scripts/split_vehicle_dataset.sh \
  datasets/splits/ackermann-v1.json \
  --seed 1000 \
  --train-ratio 0.8 \
  --validation-ratio 0.1 \
  --test-ratio 0.1 \
  datasets/exports/episode-001 \
  datasets/exports/episode-002 \
  datasets/exports/episode-003
```

输出 Schema 为 `vehicle.dataset.split.v1`。Manifest 不复制图像或帧数据，只记录：

- 随机种子和请求比例；
- 每个 Split 的完整 Episode 列表；
- Episode 相对路径、帧数和任务分布；
- `dataset_manifest.json` 与 `frames.jsonl` 的 SHA-256，用于发现源数据变化；
- Split 级 Episode、帧和任务统计。

相同输入、内容、参数和随机种子生成字节完全相同的 Manifest。重复 Episode ID、空 Episode、Schema 错误和 Manifest 帧数不一致都会被拒绝。

当 Episode 太少时，工具优先保证训练集至少一个 Episode。只有一个 Episode 时验证集和测试集为空；两个 Episode 时只能填充一个非训练 Split；当 Episode 数量足以覆盖所有非零比例时，每个启用的 Split 至少一个 Episode。Manifest 会记录空验证集或测试集警告。

## 按 Split 转换 LeRobot

切分后分别转换：

```bash
./scripts/convert_lerobot_split.sh \
  datasets/splits/ackermann-v1.json \
  train \
  datasets/lerobot/ackermann-train-v1

./scripts/convert_lerobot_split.sh \
  datasets/splits/ackermann-v1.json \
  validation \
  datasets/lerobot/ackermann-validation-v1

./scripts/convert_lerobot_split.sh \
  datasets/splits/ackermann-v1.json \
  test \
  datasets/lerobot/ackermann-test-v1
```

入口会解析 Manifest 中相对于 Manifest 所在目录的 Episode 路径，然后复用 `scripts/convert_lerobot_dataset.sh`。空 Split 会被明确拒绝，不会生成空 LeRobot Dataset。

## 推荐训练门禁

正式 Fine-tune 前至少满足：

1. 对 train、validation、test 三个转换结果分别运行 Inspector；
2. 三份报告均为 `training_readiness=true`；
3. 三个 Split 无重复 Episode ID，源 Hash 与 Split Manifest 一致；
4. 动作分布覆盖实际低速转向与停车，而不是全零或单一常值；
5. 相机标定、任务文本、状态有效率和采集场景经过人工复核；
6. 训练和模型选择只使用 train/validation，test 仅用于最终冻结评估。