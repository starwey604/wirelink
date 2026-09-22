# 可靠路径丢包基准

[English](README.md)。

这个基准测量传输丢包时可靠链路路径的表现：一次事务收敛需要多久、要花多少次
尝试、什么时候重试耗尽。它把重传、ACK、去重逻辑从真实 socket 中隔离出来，所以
每次运行都是确定、可复现的。

## 模型

两条 Wirelink link 通过一个种子化的丢包信道，一次交换一个可靠事务。link 是真实
的：跑 `wl_send_reliable`、固定 ACK 超时的重传定时、自动 ACK、去重抑制和重试
耗尽。只有信道是合成的。

- link 发出的每个 unit 以概率 `p` 丢弃，否则经过固定单向 `delay_ms` 送达。
- 两个方向独立以相同概率丢包，所以 DATA 和它的 ACK 都可能丢。
- 模拟毫秒时钟每轮前进 1 步。重传由 link 自己的 `ack_timeout_ms` 触发，所以收敛
  时间是协议时间，不是墙钟采样。
- RNG 有种子，同样的参数永远产生同样的报告。

它和 [`tests/tutorials/udp_processes.py`](../../tests/tutorials/udp_processes.py)
（真实 UDP、验正确性）、[`mixed_traffic`](../mixed_traffic/README-cn.md)
（应用层 ACK 丢包与 telemetry age）互补。

## 构建与运行

```sh
cmake -S . -B build/reliable-loss -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_RELIABLE_LOSS_BENCHMARK=ON -DWIRELINK_BUILD_EXAMPLES=OFF \
  -DWIRELINK_BUILD_PLATFORM=OFF
cmake --build build/reliable-loss --target wirelink_reliable_loss
build/reliable-loss/benchmarks/reliable_loss/wirelink_reliable_loss \
  --samples 2000 --loss 0,1,5,10,25,50
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
| `--loss LIST` | - | 逗号分隔的丢包百分比 |
| `--json PATH` | - | 输出 JSON 报告而不是 CSV |

## 输出

每个丢包档位一行 CSV（`wirelink_reliable_loss_v1`），或用 JSON 报告里的 `cases`
数组。字段：

| 字段 | 含义 |
| --- | --- |
| `completed` / `failed` | 成功 / 重试耗尽的测量事务数 |
| `mean_ms`、`p50_ms`、`p95_ms`、`p99_ms`、`max_ms` | 已完成事务的收敛时间 |
| `attempts_per_success` | 每个完成事务的 DATA 发送次数（`1 + retries`） |
| `data_drops` / `ack_drops` | 客户端→服务端 / 服务端→客户端信道丢弃的 unit 数 |
| `duplicate_rx` | 接收端已经有的 DATA 帧（主要来自 ACK 丢失） |
| `goodput_bytes_per_s` | 每模拟秒的 payload 字节数 |

时钟是模拟的，所以 `*_ms` 是协议时间：`--delay-ms 1` 且无丢包时事务正好 2 ms
收敛。`goodput` 是相对量，不是码率。

## 对比

```sh
python benchmarks/reliable_loss/compare.py before.json after.json
python benchmarks/reliable_loss/compare.py before.json after.json \
  --max-regression-percent 5
```

对比要求配置和丢包集合一致，然后按丢包档位逐指标报告。延迟、尝试次数、失败数的
增长算回退；goodput 或完成数的下降也算。由于基准是确定性的，超出导出/导入噪声的
变化就是可靠路径的真实变化。

## 解读

`duplicate_rx` 跟随 `ack_drops`：ACK 丢失时发送端重传，接收端把重传识别为重复并
重新 ACK。这次重复投递就是 ACK 丢失的代价，这里直接可见而不用推断。当
`max_retries` 对当前丢包率太小时会出现 `failed`，扫高丢包率时请调大它。

## 不测什么

- 真实 socket、OS 调度、内核队列丢包和 adapter 批处理。那些属于真实传输基准；本
  基准只隔离协议。
- 多个并发可靠事务：link 在一个可靠事务被 take 之前只保留一个。
- 重复或乱序注入、相关（突发）丢包、拥塞控制。信道是独立、保序地丢包。
- 重传路径的墙钟 CPU 成本；那用主机 CPU 基准。
- 应用/RPC 层；这里是链路级可靠事务。

## 流程

改动核心前先冻结一份 JSON 报告，记录种子和配置；改完重建再对比。两侧保持相同
参数；换种子可以检查结果不是一个丢包模式的偶然产物。
