# 可靠路径丢包基准

[English](README.md)。

这个基准测量传输丢包、重复、乱序或突发时可靠链路路径的表现：一次事务收敛需要
多久、要花多少次尝试、什么时候重试耗尽。它把重传、ACK、去重逻辑从真实 socket
中隔离出来，所以每次运行都是确定、可复现的。

## 模型

两条 Wirelink link 通过一个种子化故障信道，一次交换一个可靠事务。link 是真实
的：跑 `wl_send_reliable`、固定 ACK 超时的重传定时、自动 ACK、去重抑制和重试
耗尽。只有信道是合成的。

- **独立丢包**：每个发出的 unit 以 `--loss-percent` 概率丢弃，否则经过固定单向
  `delay_ms` 送达。
- **重复**：已送达的 unit 以 `--duplicate-percent` 概率入队两次。
- **乱序**：已送达的 unit 以 `--reorder-percent` 概率额外延迟
  `--reorder-extra-ms`，于是它排在其后发出的 unit 之后到达。
- **突发（相关）丢包**：两状态 good/bad 信道。每个 unit 以
  `--burst-start-percent` 概率进入 bad 状态；在 bad 状态里 unit 以
  `--burst-drop-percent` 概率丢弃，状态平均持续 `--burst-length` 个 unit。
- 两个方向独立受同样设置影响，所以 DATA 和它的 ACK 都可能丢、重复或乱序。
- 模拟毫秒时钟每轮前进 1 步。重传由 link 自己的 `ack_timeout_ms` 触发，所以收敛
  时间是协议时间，不是墙钟采样。
- RNG 有种子，同样的参数永远产生同样的报告。所有故障开关为 0 时，信道等价于纯
  独立丢包。

它和 [`tests/tutorials/udp_processes.py`](../../tests/tutorials/udp_processes.py)
（真实 UDP、验正确性）、[`mixed_traffic`](../mixed_traffic/README-cn.md)
（应用层 ACK 丢包与 telemetry age）互补。

## 构建与运行

```sh
cmake -S . -B build/reliable-loss -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_RELIABLE_LOSS_BENCHMARK=ON -DWIRELINK_BUILD_EXAMPLES=OFF \
  -DWIRELINK_BUILD_PLATFORM=OFF
cmake --build build/reliable-loss --target wirelink_reliable_loss
```

```sh
# 独立丢包扫。
.../wirelink_reliable_loss --samples 2000 --loss 0,1,5,10,25,50

# 仅重复 / 仅乱序。
.../wirelink_reliable_loss --loss 0 --duplicate-percent 50
.../wirelink_reliable_loss --loss 0 --reorder-percent 50 --reorder-extra-ms 15

# 相关突发：每 unit 3% 进入，约 4 个 unit，期间全丢。
.../wirelink_reliable_loss --loss 0 --burst-start-percent 3 --burst-length 4 \
  --burst-drop-percent 100

# 全开。
.../wirelink_reliable_loss --samples 2000 --loss 2 --duplicate-percent 10 \
  --reorder-percent 10 --reorder-extra-ms 15 --burst-start-percent 1 \
  --burst-length 3 --burst-drop-percent 80 --json report.json
```

选项：

| 选项 | 默认 | 含义 |
| --- | --- | --- |
| `--samples N` | 2000 | 每个丢包档位的测量事务数 |
| `--warmup N` | 32 | 窗口前不记录的事务数 |
| `--payload N` | 64 | 可靠 payload 字节数 |
| `--delay-ms N` | 1 | 单向信道延迟 |
| `--ack-timeout-ms N` | 20 | 链路 ACK 超时（每次重传固定） |
| `--max-retries N` | 20 | 事务失败前的重试次数 |
| `--seed N` | 0x574C524C | 信道 RNG 种子 |
| `--loss LIST` | - | 逗号分隔的独立丢包百分比 |
| `--duplicate-percent P` | 0 | 每个 unit 送达两次的概率 |
| `--reorder-percent P` | 0 | 每个 unit 迟到的概率 |
| `--reorder-extra-ms N` | 10 | 乱序 unit 的额外延迟 |
| `--burst-start-percent P` | 0 | 每个 unit 进入 bad 状态的概率 |
| `--burst-length N` | 4 | bad 状态平均持续的 unit 数 |
| `--burst-drop-percent P` | 0 | bad 状态期间的丢弃概率 |
| `--json PATH` | - | 输出 JSON 报告而不是 CSV |

丢包列表是被扫的变量；其它故障开关在一次运行内固定。要分别扫它们就多跑几次。

## 输出

每个丢包档位一行 CSV（`wirelink_reliable_loss_v1`），或用 JSON 报告里的 `cases`
数组。每行先重复故障配置，再给结果：

| 字段 | 含义 |
| --- | --- |
| `completed` / `failed` | 成功 / 重试耗尽的测量事务数 |
| `mean_ms`、`p50_ms`、`p95_ms`、`p99_ms`、`max_ms` | 已完成事务的收敛时间 |
| `attempts_per_success` | 每个完成事务的 DATA 发送次数（`1 + retries`） |
| `data_drops` / `ack_drops` | 客户端→服务端 / 服务端→客户端信道丢弃的 unit 数 |
| `duplicate_rx` | 接收端已经有的 DATA 帧 |
| `duplicated_units` / `reordered_units` | 信道送达两次 / 迟到的 unit 数 |
| `goodput_bytes_per_s` | 每模拟秒的 payload 字节数 |

时钟是模拟的，所以 `*_ms` 是协议时间：`--delay-ms 1` 且无故障时事务正好 2 ms
收敛。`goodput` 是相对量，不是码率。

## 对比

```sh
python benchmarks/reliable_loss/compare.py before.json after.json
python benchmarks/reliable_loss/compare.py before.json after.json \
  --max-regression-percent 5
```

对比要求配置（含故障开关）和丢包集合一致，然后按丢包档位逐指标报告。延迟、尝试
次数、失败数的增长算回退；goodput 或完成数的下降也算。由于基准是确定性的，超出
导出/导入噪声的变化就是可靠路径的真实变化。

## 解读

- `duplicate_rx` 同时包含注入的重复和接收端已有的重传。把它和干净的运行对比，就能
  把两者分开。
- **乱序**主要延迟 ACK，于是发送端超时重传，接收端随后看到重复。表现为
  `reordered_units > 0` 且 `attempts_per_success > 1`。
- **突发**是压 RTO 策略的地方：连续丢包让多次重试连续失败，所以 `p95`/`p99`/`max`
  随 `--burst-length` 增长。把失败归咎于协议之前，先调大 `--max-retries`。
- `failed` 在 `max_retries` 对当前故障水平太小时出现。

## 不测什么

- 真实 socket、OS 调度、内核队列丢包和 adapter 批处理。那些属于真实传输基准；本
  基准只隔离协议。
- 多个并发可靠事务：link 在一个可靠事务被 take 之前只保留一个。
- 实测信道模型。突发状态是合成的两状态过程，不是对真实无线/链路的拟合。
- 拥塞控制或速率自适应；信道不依赖负载。
- 重传路径的墙钟 CPU 成本；那用主机 CPU 基准。
- 应用/RPC 层；这里是链路级可靠事务。

## 流程

改动核心前先冻结一份 JSON 报告，记录种子和配置；改完重建再对比。两侧保持相同
参数；换种子可以检查结果不是一个丢包模式的偶然产物。
