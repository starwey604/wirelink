# 基准

Wirelink 的可选、可复现测量。它们都不是正确性门槛，依赖都不进入核心，数值也不构成性能承诺。
每个目录只有一份简短说明：测什么、怎么跑、不测什么。

固件基准统一跑在 ESP32-S3 DevKitC 上。测试矩阵里的 `native_sim` 和 QEMU 只用于正确性，不用于计时。

| 基准 | 测量内容 | 运行位置 |
| --- | --- | --- |
| [api](api/README-cn.md) | 两个 C-owned kernel 的 idle、8 次 RPC/拒绝与 Sample pair；含真实 RAM copy 和 owner step | 主机 |
| [rpc_validation](rpc_validation/README-cn.md) | 公开 codec 解码、canonical bytes 摘要与 owned 转换 | 主机、ESP32-S3 |
| [codec_plan](codec_plan/README-cn.md) | 字段查找与定长 packed array 的 codec 入口成本 | 主机、ESP32-S3 |
| [mixed_traffic](mixed_traffic/README-cn.md) | 请求/响应丢失与背压下，遥测年龄和显式应用重试 | 主机、ESP32-S3 |
| [framing](framing/README-cn.md) | 独立 framing 编码/alias/decode 工具 | 主机、ESP32-S3 |
| [owner_pass](owner_pass/README-cn.md) | 32 命令槽、8 命令预算与 stop 拒绝（计数验证） | 主机 |
| [fifo](../docs/fifo-performance.md) | 有序 SPSC FIFO runtime | 主机 |
| [footprint](footprint/README-cn.md) | 各模块静态代码大小与调用方存储契约 | 主机 |

`benchmarks/zephyr/` 下的[固件变体](zephyr/README-cn.md)在 ESP32-S3 DevKitC 上跑同一批负载，
两侧必须使用同一份冻结生成产物。

当前注册表有七个套件：smoke = owner_pass/mixed_traffic/footprint，quick = framing/codec_plan/rpc_validation，full 再加 api。需要代码生成时，使用匹配的本地 WLC 0.13；不能假定已经发布。ARQ、框架 Bulk、replay peer hook、旧 RX backend 与旧 executor 工作负载已删除，逐文件替代见 [删除表](retired-execution-workloads.csv)。历史报告读取器保留，但新旧工作负载不混比。物理 USB/串口工具见 [host](host/README.md)，它们不属于统一计时预设。

## 统一入口

[`bench.py`](bench.py) 读取 [`presets.toml`](presets.toml) 里的套件注册表与预设
（确定性 `smoke`、哨兵 `quick`、诊断 `full`）；`pair`、`calibrate` 按
[SPEC](SPEC-cn.md) 后续落地。目前可用命令如下：

```sh
python3 benchmarks/bench.py list      # 列出套件与预设
python3 benchmarks/bench.py doctor    # 检查预设所需工具（缺必需项则退出 2）
python3 benchmarks/bench.py run --preset smoke   # 构建、执行并收集到 out/<run-id>/
python3 benchmarks/bench.py report out/<run-id>  # 把 raw/ 归一化为 normalized.json（幂等）
python3 benchmarks/bench.py pair --suite footprint --before out/a --after out/b  # 三态退出 0/1/2
python3 benchmarks/bench.py pair --preset smoke --base origin/main --candidate . # 构建+交替+比较
python3 benchmarks/bench.py calibrate --suite footprint --runs 10  # median/MAD 噪声带
```

`doctor` 默认按 `--preset smoke` 判定必需项：Python 3.11+、CMake ≥ 3.21、
C 编译器、`size` 和 WLC。timing 套件还需要 Google Benchmark
（`--preset quick`/`--preset full`）。运行目录独占创建、绝不覆盖：其中
`manifest.json` 与权威的 `raw/`，`report` 只从 `raw/` 重建派生的
`normalized.json`。`pair` 写出 `comparison.json`、`summary.md` 与
`junit.xml`（直比模式写入 `--after` 目录，编排模式写入 `--out`），退出码
为三态；`--base`/`--candidate` 在隔离 git worktree 中构建两侧并交替执行
（默认 A/B/B/A），`--before`/`--after` 则不重新测量、仅重比较既有运行。
`calibrate` 在一台机器上重复运行同一套件并写出 `calibration.json`
（每个指标的 median、MAD 与 5×MAD 噪声下限）；记录的阈值必须是
`max(产品允许值, 噪声下限)`。普通 push/PR 不运行性能测量；手工
`benchmark.yml` workflow 提供 `smoke`、`quick` 与打 tag 前的 `release`
模式。具体流程见[发布 benchmark checklist](RELEASE-cn.md)。手工运行仍以
下方各套件指南为准。

这里说明的是怎么复现。机器相关的实测数字发布在
[文档站](https://docs.silkenkite.ink/wirelink/reference/benchmarks/)，
由人工在实测后更新，不按次提交进仓库。
