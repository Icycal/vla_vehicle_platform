# 数据采集到 LeRobot 训练数据使用说明

## 1. 数据为什么分三层

平台不会把 ROS 2 原始记录直接交给某一个模型，而是分成三层：

```text
原始 Episode
  → vehicle.dataset.v1
  → LeRobot Dataset
  → ZIP 训练包
  → x86 训练服务器
```

### 原始 Episode：采集原件

数据采集页面开始录制后，`episode_recorder` 将数据保存到：

```text
datasets/episodes/<episode-id>
```

它包含 rosbag2 数据和 `episode_manifest.json`，是采集过程的原始证据，用于完整审计、回放和重新整理。它不是 SmolVLA/LeRobot 可以直接训练的格式，也不是页面上的“下载文件”。

### `vehicle.dataset.v1`：平台中间数据

通过 Episode 导出生成：

```text
datasets/exports/<episode-id>
```

这是平台自己的、与具体 VLA 模型无关的中间格式，负责把原始 ROS 记录整理成稳定的训练样本。它可以理解为“原始采集”和“某个训练框架格式”之间的标准交换层：

- 图像和图像相对路径；
- Observation ID、Episode ID 和时间戳；
- 任务文本；
- 状态值和有效性掩码；
- 执行动作、VLA 候选动作和 Shadow 对比结果；
- 数据集 Manifest。

中间格式的意义是：以后换 SmolVLA、OpenVLA 或其他 VLA 时，不需要重新解析 rosbag2，只需要增加新的 Dataset Adapter。

### LeRobot Dataset：训练适配结果

通过 LeRobot Adapter 生成：

```text
datasets/lerobot/<dataset-name>
```

该目录包含 `meta/`、`data/`、Parquet 文件和转换 Manifest，是当前可直接交给 LeRobot/SmolVLA 训练脚本的训练格式。它已经不是原始数据，而是针对 LeRobot 生态的适配结果；未来接入 OpenVLA 或其他框架时，可以从同一份 `vehicle.dataset.v1` 生成对应格式。

## 2. 推荐网页操作流程

访问车端控制台：

```text
http://10.101.70.232:8088/#capture
```

### 第一步：采集 Episode

1. 打开“数据采集”页面；
2. 填写 Episode ID；
3. 填写任务描述，例如：`沿通道前进并避开障碍物`；
4. 填写操作员 ID；
5. 点击“开始数据采集”；
6. 进行真实车辆操作、导航操作或 Shadow 观察；
7. 完成后点击“停止数据采集”。

录制过程中不要在存储管理页面删除当前 Episode。

### 第二步：生成中间数据

进入“工程工具”页面，在“Episode 与数据集管理”中：

1. 在左侧“原始 Episode”找到刚采集的 Episode；
2. 点击“生成中间数据”；
3. 系统通过白名单 Job 执行 `scripts/export_episode.sh`；
4. 成功后，中间数据出现在中间栏：

```text
datasets/exports/<episode-id>
```

也可以在页面下方的 Job 输出区查看日志。

### 第三步：选择来源并转换 LeRobot

在“训练数据流水线”区域：

1. 在中间数据栏勾选一个或多个 `vehicle.dataset.v1` 数据集；
2. 填写 LeRobot 数据集名称，例如：

```text
ackermann-train-v1
```

3. 点击“转换为 LeRobot”；
4. 系统提交 `dataset.convert_lerobot` 后台 Job；
5. 转换完成后，右侧出现 LeRobot 数据集；
6. 点击“详情”确认文件、帧数、Manifest 和训练兼容性。

输出目录为：

```text
datasets/lerobot/ackermann-train-v1
```

当前转换器支持多个中间数据源作为输入，适合把多个 Episode 合并为一个训练数据集。不同来源必须使用同一套 Dataset Mapping 和兼容的字段结构。

### 第四步：生成训练包

LeRobot 数据集目录是车端的最终训练格式，但训练通常在 x86 服务器上进行。因此还需要生成 ZIP：

1. 在右侧“LeRobot 数据集”勾选一个或多个数据集；
2. 点击“生成训练包”；
3. 系统执行 `dataset.archive_lerobot` 后台 Job；
4. 训练包生成后，页面显示“下载到 x86”；
5. 点击下载，浏览器会携带操作令牌从车端受保护接口下载 ZIP。

训练包保存于：

```text
run/ops/exports/<dataset-name>.zip
```

下载后的 ZIP 解压后就是 LeRobot 数据集目录，可以复制到 x86 训练机。

## 3. 命令行等价流程

如果不使用网页，可以在 Orin 上执行：

```bash
cd /home/wheeltec/vla_vehicle_platform

./scripts/export_episode.sh \
  datasets/episodes/episode-001 \
  datasets/exports/episode-001
```

质量检查：

```bash
./scripts/inspect_dataset.sh \
  datasets/exports/episode-001 \
  run/test/dataset-quality/episode-001
```

转换为 LeRobot：

```bash
./scripts/convert_lerobot_dataset.sh \
  datasets/lerobot/ackermann-train-v1 \
  datasets/exports/episode-001 \
  datasets/episode-002
```

生成训练包：

```bash
./scripts/archive_dataset.sh \
  run/ops/exports/ackermann-train-v1.zip \
  datasets/lerobot/ackermann-train-v1
```

## 4. 在 x86 训练机上使用

将网页下载的 ZIP 复制到 x86 训练机，例如：

```bash
mkdir -p ~/datasets/lerobot
unzip ackermann-train-v1.zip -d ~/datasets/lerobot
```

确认目录结构：

```text
ackermann-train-v1/
  meta/info.json
  meta/stats.json
  meta/tasks.parquet
  data/
```

然后在 x86 的 LeRobot/SmolVLA 训练环境中，将数据集路径指向解压后的目录。当前车端只负责采集、整理、转换和导出，不建议在实车上进行长时间训练。

## 5. 当前页面各按钮的含义

| 页面按钮 | 输入 | 输出 | 作用 |
|---|---|---|---|
| 原始 Episode“详情” | Episode 目录 | 文件统计和 Manifest | 查看原始记录 |
| 原始 Episode“导出” | 一个 Episode | `vehicle.dataset.v1` | 原始数据转中间格式 |
| 中间数据“详情” | 中间数据目录 | 文件统计和 Manifest | 检查转换结果 |
| 中间数据“转 LeRobot” | 一个中间数据 | LeRobot 目录 | 快速单数据集转换 |
| 训练流水线“转换为 LeRobot” | 勾选一个或多个中间数据 | 指定名称的 LeRobot 目录 | 多 Episode 合并转换 |
| LeRobot“导出训练包” | 一个或多个 LeRobot 目录 | ZIP | 准备下载到 x86 |
| “下载到 x86” | ZIP 名称 | 浏览器下载 | 获取最终训练包 |

## 6. 注意事项

- 原始 Episode 不能直接当作 LeRobot 数据集使用；
- `vehicle.dataset.v1` 是中间格式，不是最终训练格式；
- LeRobot 目录才是当前模型训练框架可以直接加载的最终数据；
- ZIP 只是跨机器传输封装，解压后仍然是 LeRobot 数据集；
- 训练前必须运行数据质量检查；
- 只有真实有效、动作非零、任务标注清晰的数据才适合训练；
- 当前 Smoke 数据主要用于验证转换和加载接口，不能代表有效训练集；
- 下载和写入操作需要 Vehicle Ops 操作令牌；
- ZIP 下载接口限制单个文件大小，超大数据集应通过硬盘、SFTP 或对象存储传输；
- 生产环境建议把 ZIP 导出、下载和训练放到后台 Job 或 x86 数据服务器，避免占用 Orin 磁盘和内存。
