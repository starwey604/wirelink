# RX ring backend 基准（ESP32-S3）

[English](README.md)。

`rx_backend` 在实板上测量 Wirelink 的接收字节路径：原子 SPSC BipBuffer
（`src/rx_ring_bipbuf.c`）、`wl_rx_reserve` / `wl_rx_commit` / `wl_feed_bytes`
原语，以及 Zephyr async UART DMA adapter（`adapters/zephyr/uart_dma`）。
构建和抓取方式与 [../README-cn.md](../README-cn.md) 中的固件基准一致，这里只
写这个 app 特有的内容。

它是硬件夹具，不是计时门禁。Twister 只对三种 ingress 做 build，不运行；
`native_sim` 和 QEMU 仍然是正确性门禁，不作为时钟。

## Ingress 模式

`WIRELINK_BENCH_INGRESS` 选择字节如何进入 RX ring：

| 取值 | 生产者 |
| --- | --- |
| `IRQ`（默认） | UART1 RX 中断每次最多读 `BENCH_UART_CHUNK`（64）字节进 FIFO，调用 `wl_feed_bytes` |
| `DMA` | Zephyr async UART DMA adapter 接管 UART1 RX；主循环 service adapter 并 poll link |
| `USB` | CDC ACM 中断 ingress；需要 `EXTRA_CONF_FILE=usb.conf`，由主机发帧 |

非 USB 模式用 UART0 TX 产生流量，只按上限分块填 FIFO，不使能 TX 中断。
ESP32-S3 只有一个 UHCI0 引擎，UART0 async TX 与 UART1 async RX 无法同时工
作，所以源 UART 走普通 FIFO 写。

## 负载

`WIRELINK_BENCH_STREAM` 选择负载：

- **停止等待**（默认）：发一帧、等它送达、记录单帧延迟，再重复。输出
  `wirelink_rx_bench_v1` 行。帧与帧之间线路空闲。
- **连续流**（`=ON`）：持续填满 TX FIFO，同时并发 drain RX，线路一直忙，
  adapter 的 re-arm 路径处于持续负载下。输出 `wirelink_rx_stream_v1` 行。

两种负载都会先跑不碰 UART 的 `wirelink_rx_primitive_v1`（reserve/commit 与
feed 微基准），把 ring 本身与 ingress 分开测量。连续流在 `USB` 模式下不可用。

## 构建与烧录

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rx_backend -d build/rx-hw -- \
  -DZEPHYR_EXTRA_MODULES=/path/to/wirelink \
  -DWIRELINK_BENCH_INGRESS=DMA -DWIRELINK_BENCH_STREAM=ON
west flash -d build/rx-hw --esp-device /dev/ttyACM0
python benchmarks/zephyr/capture_serial.py --port /dev/ttyACM0 \
  --out stream.log --until "wirelink_rx_bench_v1,error" --seconds 180
```

console 是板载 USB Serial/JTAG（Linux 上是 `/dev/ttyACM0`）。UART bridge 是另
一个口；串口布局和 udev 规则见 [../README-cn.md](../README-cn.md)。

可选 CMake 选项：

| 选项 | 作用 |
| --- | --- |
| `WIRELINK_BENCH_STREAM=ON` | 改用连续流负载 |
| `WIRELINK_BENCH_RING=N` | 覆盖可用 RX ring 大小（默认 4096） |
| `WIRELINK_BENCH_DMA_CHUNK=N` | 覆盖最大 direct DMA claim（默认等于 ring） |
| `WIRELINK_BENCH_STREAM_FRAMES=N` | 每个连续流 profile 的测量帧数（默认 2000） |
| `WIRELINK_BENCH_PAYLOAD_START=I` | 首个 payload profile 下标（默认 0：16 B） |
| `WIRELINK_BENCH_PAYLOAD_END=I` | 最后一个 payload profile 的后一位（默认全部） |

payload profile 为 16、20、64、120、256、1024、2048 字节。`START` / `END` 是
缩短冒烟运行的方式。其它 `BENCH_*` 宏（warmup、帧超时、UART idle 超时、chunk
大小）可通过 `-DCMAKE_C_FLAGS=-DBENCH_...` 设置。

## 输出

`wirelink_rx_bench_v1,meta,...` 记录时钟、ring 大小、物理 ring 大小和
fallback 大小。随后是表头，再按所选负载每个 payload 一行。

`wirelink_rx_stream_v1` 列：

| 列 | 含义 |
| --- | --- |
| `wire_bytes` | 发出的线上字节（含 warmup 的所有帧） |
| `received` | 已送达且 payload 校验通过的帧数 |
| `run_cycles` | 首帧入队到末次有效送达，单位硬件 cycle |
| `producer_calls` / `producer_cycles` | IRQ：`wl_feed_bytes` 调用数与 CPU cycle。DMA：adapter RX-ready 事件数与发布 cycle |
| `accepted` | 被 ring 接收的字节数 |
| `dropped` / `overflow` / `malformed` / `bad_integrity` | ingress 与 link RX 计数器 |
| `adapter_errors` | DMA adapter 错误（仅 DMA ingress） |
| `latency_*` | 入队到送达的中位数 / p95 / p99 / 最大值，只统计测量帧 |

`run_cycles` 由毫秒 uptime 换算，因为 32 位 ESP32 硬件计数器在长连续流中会回
绕。单帧延迟仍用硬件计数器。

## 夹具与对比

- ESP32-S3-DevKitC，`esp32s3_devkitc/esp32s3/procpu`，240 MHz。
- UART0 TX GPIO16 直连 UART1 RX GPIO18，3 Mbaud 8-N-1，无流控。
- 夹具不接其它线；不要接 UART0 RX 或 UART1 TX。

改动 RX 路径前先冻结 ELF、`.config` 和 SHA-256，candidate 在另一个目录构建，
并保持相同的 payload 集合、ring 与 claim 大小、UART 夹具。

## 不测什么

- 不做流控、不做刻意背压，也不做对抗性坏帧；默认连续流只通过生产者/消费者
  的自然速率差给消费者加压。
- USB 数据 ingress 这里只做 build，不能与 UART 数字相比。
- 只覆盖 ESP32-S3 UHCI0 这一种形态。有独立 UART DMA 引擎、能重叠 TX/RX 的
  平台不在覆盖范围内。
- 数字与夹具、版本强相关，不是 wire 延迟或可移植性结论。

## 连续流现状

连续流会暴露停止等待看不到的路径。触发条件不是“线路 100% 占空”：只要对端
在没有任何 ≥ 配置超时的空闲间隙的情况下，连续发送超过一个 direct claim
（默认 4096 字节），finite-timeout 的 UART DMA ingress 就会丢帧。`IRQ`
ingress 能完成所有 payload；`DMA` ingress 从第一个满 claim 起就开始丢帧，并
随 payload 增大而恶化。`received` 小于帧数就应视为失败，不是噪声。该负载被
保留，用于验证后续的连续/突发修复；`received == frames` 且 `overflow`、
`malformed` 均为 0 是通过条件。机制、证据和修复约束见
[../../../docs/rx-performance.md](../../../docs/rx-performance.md) 的
continuous/burst RX failure 一节。
