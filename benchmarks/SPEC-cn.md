# Benchmark 规范

[English](SPEC.md).

本文件是 Wirelink benchmark 体系的规范性契约：结果 schema、退出码、输出
目录结构、比较规则与 CI 分层。政策背景见
[docs/development.md](../docs/development.md)（"Benchmark policy"）；各套件
的用法见 `benchmarks/<suite>/README.md`。

Benchmark 保持 opt-in，永远不是正确性门。本文件增加的是：让*性能回归门*
以可预测的方式失败——在同一 testbed 上、带着人类可审计的证据。

## 统一入口

所有套件通过一个 Python 3.11+ 入口驱动（无第三方依赖，`tomllib` 读取预设
文件）：

```sh
python3 benchmarks/bench.py doctor      # 工具链/依赖/串口检查
python3 benchmarks/bench.py list        # 套件与预设
python3 benchmarks/bench.py run --preset <name> [--out DIR]
python3 benchmarks/bench.py pair --preset <name> --base REV --candidate REV [--order ab|abba]
python3 benchmarks/bench.py pair --preset <name> --before DIR --after DIR
python3 benchmarks/bench.py report RUN_DIR
python3 benchmarks/bench.py calibrate --preset <name> --runs N
```

- `run`：按预设配置 CMake、构建、执行并收集原始证据；随后运行 `report`
  生成 `normalized.json`。
- `pair`：用各套件自己的 comparator 比较两次运行，产出
  `comparison.json`、`summary.md` 与 `junit.xml`。给 `--base`/`--candidate`
  时使用 candidate 一侧的 driver 与预设在隔离源码树中构建两侧，因此
  base 可以早于 `bench.py`，并交替执行（默认 A/B/B/A，同一指标取两个
  bracket 中更差的状态）；给 `--before`/`--after` 时仅重新比较既有运行
  目录、不重新测量（幂等，输出写入 `--after` 目录）。
- `report`：仅凭 `raw/` 重新生成 `normalized.json`，不重新测量（幂等）；
  比较类输出由 `pair` 重新生成。
- `calibrate`：在同一 testbed 上重复运行同一 baseline，输出阈值推导所
  须的 median/MAD 噪声带。

统一 HIL 命令仍在规划中；固件套件当前使用各自的构建、烧录与采集说明。

预设声明在 `benchmarks/presets.toml`。每个套件条目声明其 `kind`
（`deterministic`、`timing`、`size`）、所需 CMake 选项、命令与 gate
profile。初始预设：`smoke`、`quick`、`full`。

## 退出码

每个 `bench.py` 子命令与每个 `compare.py` 使用同一三值契约：

| 退出码 | 含义 |
| --- | --- |
| `0` | 通过：测量有效，所有启用的 gate 成立 |
| `1` | 回归：测量有效，某个 gate 阈值被突破 |
| `2` | 无效：测量缺失、不完整、不匹配或不可比——不意味着任何回归结论 |

退出码 `2` 永远不是性能结论。它表示本次运行没有产生可信的比较，CI 必须
把它当作"需要重跑或排查的失败运行"，而不是通过或回归。

## 输出结构

每次执行写入一个目录；已存在的目录永不覆盖（保留证据，与
`benchmarks/api/run.py` 一致）：

```text
out/<run-id>/
  manifest.json       # 运行身份：revision、testbed、environment
  raw/                # 未改动的原始输出（Google Benchmark JSON、
                      #   串口日志、各套件原生报告）
  normalized.json     # wirelink-benchmark-v2 测量
  comparison.json     # 机器可读判定（仅 pair/report）
  summary.md          # 供 $GITHUB_STEP_SUMMARY 或 PR 评论的摘要
  junit.xml           # 每个被 gate 的指标一个测试用例
  artifacts/          # binaries.sha256、CMakeCache.txt、firmware.elf、
                      #   firmware.map、zephyr.config（按需）
```

`raw/` 是权威数据。归一化与报告必须能仅凭 `raw/` 通过
`bench.py report` 复现；报告不得隐藏或篡改原始数据。

`manifest.json` 使用 schema `wirelink-benchmark-manifest-v1`，与目录其余部分
一样只创建不覆盖。各套件直接采集的 Google Benchmark JSON 通过
`benchmarks/stamp.py` 记录负载身份（`context.benchmark_source_hash`）；
所有原生 comparator 与归一化层适用同一条双侧规则：哈希不同即
`incomparable`，绝不给出回归结论。

## 结果 schema：wirelink-benchmark-v2

`normalized.json` 是所有套件转换成的唯一逻辑模型。套件原生格式保留在
`raw/`；只有这一层是统一的。

```json
{
  "schema": "wirelink-benchmark-v2",
  "testbed": "github-ubuntu-x64",
  "revision": "<git sha>",
  "environment": {
    "compiler": "gcc (Ubuntu ...) 13.2.0",
    "flags": "-O3 -DNDEBUG",
    "cmake_options": "WIRELINK_BUILD_FRAMING_BENCHMARK=ON;...",
    "cpu": "<model>",
    "affinity": "<绑定的 CPU 列表，可为空>",
    "governor": "<performance/schedutil/unknown>",
    "wlc": "<版本与源摘要，可为空>",
    "benchmark_source_hash": "<该套件 workload 源码的 sha256>"
  },
  "measurements": [
    {
      "suite": "framing",
      "case": "retry3/e0/i2/p2/b512",
      "metric": "cpu_ns",
      "unit": "ns",
      "direction": "lower",
      "samples": [120.1, 121.8, 119.7],
      "median": 120.1
    }
  ]
}
```

规则：

- `direction` 为 `lower`、`higher` 或 `exact`。`exact` 覆盖 storage
  contract 字节数与公共结构体尺寸：任何变化都是发现项。
- `samples` 保存该套件产生的全部重复；`median` 是 gate 统计量。百分位
  指标（p99）在 `metric` 中记录其估计方式。
- `benchmark_source_hash` 覆盖 workload 自身源码。两份报告该值不同则该
  对**不可比**：退出码 `2`，判定 `incomparable`，不得把数字呈现为回归。
- 同样的不可比规则适用于 `testbed`、`unit`、`direction`、case 集合、重
  复次数，以及固件的时钟/`hz` 与批次大小。各套件继续强制其既有的
  workload 不变式。

## 比较与 gate

`comparison.json` 对每个被 gate 的指标记录：

```json
{
  "suite": "framing",
  "case": "retry3/e0/i2/p2/b512",
  "metric": "cpu_ns",
  "direction": "lower",
  "before": 120.1, "after": 126.5,
  "delta_abs": 6.4, "delta_rel": 0.053,
  "threshold": {"rel": 0.10, "abs": 5.0},
  "severity": "warn",
  "status": "pass"
}
```

- `status` 为 `pass`、`warn`、`fail` 或 `incomparable`。
- timing 指标只有在**同时**突破相对阈值与绝对阈值时才算回归，因此
  1 ns → 2 ns（100%）本身不会失败。默认 profile 为
  `relative > 10% AND absolute > 5 ns`。
- 初始阈值绝不拍脑袋。取
  `max(产品允许变化, 5 × 实测噪声带)`，噪声带来自 gating testbed 上
  `bench.py calibrate --runs 20` 的 MAD。
- gating 只用每套件 10~20 个 sentinel case。完整矩阵仅用于诊断与趋势，
  永不失败构建。
- 严重程度：`info`（仅记录）、`warn`（标注但不阻塞）、`fail`（在声明阻塞
  的层级中阻塞）。

按指标类型的阈值分类：

| 指标类型 | 规则 |
| --- | --- |
| storage contract、公共结构体尺寸 | `exact`；任何变化都是发现项 |
| 确定性协议指标（reliable loss、mixed traffic、owner pass） | exact 或非常严格的相对界 |
| 链接后 flash/RAM（footprint `--elf`） | 绝对字节 **加** 百分比 |
| 主机 CPU timing | PR 仅 `warn`；专用 runner 可 `fail` |
| UDP/USB p99 | 趋势与报告；永不阻塞 PR |
| 固件 cycles | 频率与配置固定后可 `fail` |

## Testbed

`testbed_id` 标识一个稳定的测量环境（机型级别 + 工具链 + 固定设置），
例如 `github-ubuntu-x64` 或 `esp32s3-devkitc-01`。不同 testbed、工具链或
时钟配置的报告永不互相比较；比较以退出码 `2` 结束。固件报告还须记录板
卡身份，因为两块 ESP32-S3 是两个 testbed。

## 自动化分层

| 层级 | 内容 | 自动运行？ |
| --- | --- | --- |
| 普通 push/PR | 只运行 benchmark 工具单元测试，不做性能测量 | 是 |
| 手工 smoke | 确定性模型、storage contract、代码尺寸 | 否 |
| 手工 quick | 精选 framing/codec/RPC 主机计时 | 否 |
| tag 前 release | 对比上一个 tag，运行 smoke + quick + API/UDP | 否 |
| 未来 HIL | ESP32-S3 cycles、RX backend、USB/CDC/UART transport | 否 |

说明：

- `.github/workflows/benchmark.yml` 只支持 `workflow_dispatch`，普通 push
  和 PR 不消耗性能测量所需的 runner 时间。
- `release` 模式覆盖当前全部 8 个主机套件。GitHub 托管 runner 的计时只
  作诊断；权威发布比较应在与基线相同的固定机器和工具链上运行。
- 初始 `smoke` 预设对确定性指标采用方向性零退化容忍：任何变差都会
  要求发布审核，而改善仍然通过。
- 必须先完成比较再创建发布 tag；checklist 与证据政策见
  [RELEASE-cn.md](RELEASE-cn.md)。

## 落地状态

共享注册表、三态比较、报告、校准与手工 release workflow 已落地。早期
开发阶段有意关闭普通 push/PR 性能测量。统一 HIL 命令与固定专用 runner
仍是后续工作。在某个套件完成转换之前，其 `compare.py` 仍可直接使用，
其原生输出仍是 `raw/` 中的权威数据。
