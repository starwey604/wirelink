# 基准

Wirelink 的可选、可复现测量。它们都不是正确性门槛，依赖都不进入核心，数值也不构成性能承诺。
每个目录只有一份简短说明：测什么、怎么跑、不测什么。

固件基准统一跑在 ESP32-S3 DevKitC 上。测试矩阵里的 `native_sim` 和 QEMU 只用于正确性，不用于计时。

| 基准 | 测量内容 | 运行位置 |
| --- | --- | --- |
| [api](api/README-cn.md) | 生成端点的 CPU 与内存、loopback RPC、双进程 UDP 延迟 | 主机 |
| [api/EXECUTOR-cn.md](api/EXECUTOR-cn.md) | 多生产者 executor 的交接、唤醒与 LATEST 替换 | 主机 |
| [rpc_validation](rpc_validation/README-cn.md) | 解码、规范指纹与 owned 转换成本 | 主机、ESP32-S3 |
| [codec_plan](codec_plan/README-cn.md) | 字段查找与定长 packed array 的 codec 入口成本 | 主机、ESP32-S3 |
| [mixed_traffic](mixed_traffic/README-cn.md) | ACK 丢失与背压下遥测年龄和 RPC 完成 | 主机、ESP32-S3 |
| [framing](framing/README-cn.md) | 单趟 COBS 编码与已编码重传复用 | 主机、ESP32-S3 |
| [owner_pass](owner_pass/README-cn.md) | 有界 dispatch、空检查和交接（靠计数而非计时） | 主机 |
| [rpc_peer](rpc_peer/README-cn.md) | 可靠 RPC 请求前生成的 steady-session 守卫 | 主机 |
| [bulk](../docs/bulk-performance-cn.md) | 顺序 bulk 状态机与配对 raw sink 的对照 | 主机 |
| [fifo](../docs/fifo-performance.md) | 有序 SPSC FIFO runtime | 主机 |
| [footprint](footprint/README-cn.md) | 各模块静态代码大小与调用方存储契约 | 主机 |
| [reliable_loss](reliable_loss/README-cn.md) | 丢包下可靠路径的收敛、尝试次数与失败数 | 主机 |
| [zephyr/rx_backend](zephyr/rx_backend/README-cn.md) | RX ring backend 行为 | ESP32-S3 |

`benchmarks/zephyr/` 下的[固件变体](zephyr/README-cn.md)在 ESP32-S3 DevKitC 上跑同一批负载，
两侧必须使用同一份冻结生成产物。

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
