# UART RX timeout 优化与实测结果

测量日：2026-10-14。

本次 ESP32-S3、1 Mbaud、8N1 的短控制请求场景推荐使用 **2 字符周期**。
与独立重建的 22 基线相比，RTT p50 从 **1216.82 µs 降到 817.33 µs**（减少
**399.49 µs／32.83%**），p99 从 **1248.77 µs 降到 848.77 µs**（减少
**400.00 µs／32.03%**）。连续流量的吞吐、每帧 CPU 开销与 callback 次数基本不变，
四档共 12 万个连续样本均完整到达。

驱动保留默认 22，通过可选配置启用 2；没有修改 Wirelink core、WLC、帧格式或 FIFO 阈值。
最终两块板已刷入本次测量的 timeout=2、CPU/IRQ 探针关闭固件，并通过一次交付复测。

## 方法与计时边界

硬件是原有两块 ESP32-S3，240 MHz、1 Mbaud、8N1、单个在途请求、CRC16 + COBS。
RX FIFO=22、TX FIFO=1、16 KiB ICache、32 KiB DCache、Os、LTO off、ASSERT on、
1024 B RX Bipbuffer、owner event budget=64、main priority=0、100 µs tick 均不改变。
Zephyr `ca10f7fc7a5b5b1129823cfc57506e81bb109743`；hal_espressif
`b16851b3c20e6fad3d2da42ec24a3e82d0b8b444`；Xtensa GCC 14.3.0。

对照值为 22、8、4、2 字符周期。所有构建使用同一份完整冻结源代码、同一个匹配 WLC
可执行文件、相同生成代码、依赖实际 HEAD 与 dirty patch。每组每块板的全部 Kconfig
只允许 `CONFIG_UART_ESP32_RX_TIMEOUT` 一个差异；CPU/IRQ 开关与流量方式只在不同
测量集合之间改变。同一集合内全部保持一致。

延迟集合关闭 thread runtime stats、tracing 和 UART callback 探针。每值三次独立
硬件复位，每次 100 warmup + 1000 RPC + 1000 个 2 ms 间隔样本。按 22→8→4→2
顺序测量，再重测一次 22 检查漂移。RTT 从板上 client API 调用前，到成功响应校验后，
用板上硬件 cycle counter 计时，包含双向传输。主机不承担业务 RPC，也不用主机时间戳。
主机同时进行构建只影响烧录／日志收集；不完整的日志会被拒绝。

CPU/IRQ + 连续流量集合独立编译，开启 thread/idle accounting 与 UART callback 探针。
每值三次复位、100 warmup + 1000 RPC + 10000 个连续样本，顺序 2→4→8→22。
连续发送没有固定 pacing；每帧接受后推进 owner，等待 TX memory-consumed 背压再继续。
接收端验证所有序号和内容，最终 RPC fence 验证完整到达。日志只在测量结束后输出。
连续吞吐统计 sender admission batch，含 memory-consumed 背压；不含最后 FIFO 字节
全部移出导线的时间，也不是同步时钟的单向延迟。CPU-on 的 RTT 不用于延迟表。

`callback_irqs` 每次有效 callback 进入计一次。此驱动在非零中断状态下调用 callback
一次，因此它反映所测 UART 的非伪中断进入次数；不包含系统 tick 或 console UART。
`rx_irqs/tx_irqs` 是 callback 内就绪源处理次数，不能相加当硬件 IRQ 数。
`callback_ticks` 从 callback 锁获取／有效 binding 检查之后，到锁释放之前，
不含驱动 ISR 的前后段和锁获取／释放，也不等于整个中断入口 CPU 时间。
CPU active 含全部非 idle 活动；callback 耗时是其中子集，不能再次加到 active 上。
server CPU fence 包含一个边界响应／请求，所以 server IRQ 总数通常比 client 多几个。
各表按三轮逐指标中位数汇总；原始三轮均保留。


## RTT：关闭 CPU／IRQ 探针

基线为已经完成审核修复和 ESP32 IRAM 放置的当前 Wirelink + WLC 0.9，
不是 round-1 重构前的 0.8 基线。本实验不替换此前固定 timeout=22 的重构验收数据。

| RX timeout（字符） | p50（µs） | p99（µs） | p50 减少（µs） | p99 减少（µs） | RPC/s |
|---:|---:|---:|---:|---:|---:|
| 22 | 1216.82 | 1248.77 | 0.00 | 0.00 | 816.80 |
| 8 | 936.82 | 968.77 | 280.00 | 280.00 | 1059.00 |
| 4 | 856.82 | 888.77 | 360.00 | 360.00 | 1157.02 |
| 2 | 817.33 | 848.77 | 399.49 | 400.00 | 1212.56 |

22 的后置复测 p50/p99 仍为 1216.82/1248.77 µs；2 的最终交付复测仍为
817.33/848.77 µs。每个实测 RPC 的双向 wire bytes 一致，均值 53.056 B。
从逐次 RTT 扣除对应 wire occupancy 后，2 的 residual p50 约 282.75 µs；
该 residual 仍含尾部 timeout、调度及协议处理，不能全部视为 CPU 计算时间。

22→8/4/2 的实测净收益与双向尾部 timeout 的 280/360/400 µs 预算吻合，
说明在此固定短帧 workload 中，它是主要剩余延迟来源。这里测的是控制变量对 RTT
的净影响，没有用逻辑分析仪直接分解每帧尾部等待时间。

## CPU 与 IRQ：独立开启探针的 RPC 集合

| RX timeout | client active（µs/RPC） | server active（µs/RPC） | client owner（µs/RPC） | server owner（µs/RPC） | client CPU（%） | server CPU（%） |
|---:|---:|---:|---:|---:|---:|---:|
| 22 | 283.12 | 207.45 | 237.02 | 172.98 | 22.81 | 16.70 |
| 8 | 283.12 | 207.45 | 237.02 | 172.98 | 29.46 | 21.56 |
| 4 | 283.12 | 207.45 | 237.02 | 172.98 | 32.13 | 23.52 |
| 2 | 283.10 | 207.22 | 237.01 | 172.88 | 33.65 | 24.60 |

| RX timeout | client callbacks/1000 RPC | server callbacks/1000 RPC | client callback（µs/RPC） | server callback（µs/RPC） |
|---:|---:|---:|---:|---:|
| 22 | 2999 | 3003 | 37.93 | 25.75 |
| 8 | 2999 | 3003 | 37.93 | 25.75 |
| 4 | 2999 | 3003 | 37.93 | 25.75 |
| 2 | 2999 | 3003 | 37.92 | 25.65 |

每 RPC 的 active/owner CPU 和 IRQ 次数基本不变，微小差异不作为 CPU 优化收益。
连续调用的等待时间缩短，所以同一秒内完成更多 RPC：client CPU 占用从 22.81% 到
33.65%，server 从 16.70% 到 24.60%；client callback 频率从约 2417/s 到 3564/s，
server 从约 2417/s 到 3565/s。这是操作频率提高，并非每个 RPC 增加了 IRQ。
CPU-on 绝对数值包含统计开销，只比较本集合中的四档，不混入上面的无探针 RTT。

## 连续流量：独立开启 CPU／IRQ 探针

每次复位连续发送 10000 帧，测量窗口约 2.8 s，无固定间隔，发送端推进 owner 并等待
TX memory-consumed 背压。每个 timeout 三轮，receiver 验证全部序号及内容。

| RX timeout | admission 帧/s | client active（µs/帧） | server active（µs/帧） | client CPU（%） | server CPU（%） | client/server callbacks/10000 帧 |
|---:|---:|---:|---:|---:|---:|---:|
| 22 | 3571.65 | 47.94 | 84.08 | 17.12 | 30.02 | 10000/12177 |
| 8 | 3571.65 | 47.94 | 84.08 | 17.12 | 30.02 | 10000/12177 |
| 4 | 3571.65 | 47.94 | 84.08 | 17.12 | 30.02 | 10000/12177 |
| 2 | 3571.65 | 47.94 | 84.08 | 17.12 | 30.02 | 10000/12177 |

四档 client/server callback 耗时分别约 7.35/14.67 µs/帧。减小 timeout 没有在这条
连续流上增加 callback 数量或降低吞吐。该结论覆盖本次样本大小、串口格式与发送方式；
没有测量人为制造字节间空隙、其他波特率或长时间 soak。

## 平台实现与文档

驱动原来在 `uart_esp32_configure()` 写死 `uart_hal_set_rx_timeout(..., 0x16)`。
本地补丁仅引入 `CONFIG_UART_ESP32_RX_TIMEOUT`，默认仍为 22，范围 1..126。
第二轮前置修复增加了按 SoC/时钟容量及请求字符格式的校验：超限返回 `-EINVAL`，
在改变字符格式或清空 FIFO 前拒绝，不写入截断阈值。ESP32-S3 的 8N1 上限为 102，
8E2 上限为 85；benchmark builder 的固定 8N1 场景只接受 1..102。
这里保留的四档性能结果属于修复前历史实验，修复后的构建和复测单独保存于
`/home/ww/codings/wirelink-codec-rpc-round-2/`，不覆盖原实验。
它作用于所有该驱动管理的 UART，不改 FIFO 阈值、帧格式、CRC 或 Wirelink core。
0 不在范围内，避免关闭尾部 timeout 后短帧停留在未满 FIFO 中。

[Espressif UART API 文档](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/api-reference/peripherals/uart.html#_CPPv419uart_set_rx_timeout11uart_port_tK7uint8_t)
说明 timeout 使用字符周期。此处实际 HAL `uart_hal_set_rx_timeout()` 用
`uart_hal_get_symb_len()` 乘以 timeout，再写 ESP32-S3 的 bit-period threshold；
8N1 的 start+8 data+1 stop 共 10 位。因此 1 Mbaud 下 22/8/4/2 分别对应
220/80/40/20 µs 的设置预算，两方向合计减少 280/360/400 µs。
这些预算与实测 RTT 差分分开列出。没有接逻辑分析仪逐帧测最后一位到 RX ISR 的间隔。

## 配置与复现

本地 Zephyr 补丁已经应用于 `/home/ww/zephyrproject/zephyr`，并保存在
[esp32-uart-rx-timeout.patch](../adapters/zephyr/patches/esp32-uart-rx-timeout.patch)。
重新 `west update` 后按[补丁说明](../adapters/zephyr/patches/README.md)检查并应用。

应用可选用 [esp32-low-latency.conf](../adapters/zephyr/uart_irq/esp32-low-latency.conf)：

```ini
CONFIG_UART_ESP32_RX_TIMEOUT=2
```

例如将它加入 Zephyr 应用的 `EXTRA_CONF_FILE`；它仅改变 timeout，不开启诊断探针。
已有应用中的 RX/TX FIFO 配置保持原值。

Benchmark builder 新增三个参数，使用已冻结的相同源码与依赖执行四档对照：

```sh
timeout_source=/home/ww/codings/wirelink-uart-timeout-20261014/candidates/experiment-01
timeout_common=(
  --zephyr-base /home/ww/zephyrproject/zephyr
  --west /home/ww/zephyrproject/.venv/bin/west
  --wirelink "$timeout_source/wirelink" --wlc-source "$timeout_source/wlc"
  --wlc /home/ww/codings/wirelink-execution-round-1/build/final-02-off/wlc-host/release/wlc
  --profile scenario-uart --implementation wirelink
  --optimization size --lto off --console uart-bridge
)
# RTT：使用新的输出目录。
python3 "$timeout_source/harness/scripts/build-esp32s3.py" "${timeout_common[@]}" \
  --uart-rx-timeout 2 --cpu-stats off --out /new/build/timeout-2-off

# CPU、callback 计数与连续流量：独立固件集合。
python3 "$timeout_source/harness/scripts/build-esp32s3.py" "${timeout_common[@]}" \
  --uart-rx-timeout 2 --cpu-stats on --uart-irq-probe \
  --telemetry-mode continuous --samples 10000 --out /new/build/timeout-2-on-stream
```

对照时只把 `--uart-rx-timeout` 改为 22、8、4、2，每组新建输出目录。
`uart_experiment` 日志与 artifact `.config` 都参与校验；错误 timeout、探针配置或流量
模式会拒绝 capture。旧的未设置 timeout 参数／未应用补丁的历史 captures 仍可分析。

## 证据与验证

完整证据目录：`/home/ww/codings/wirelink-uart-timeout-20261014`。

- `candidates/before/`：改动前完整 dirty Wirelink、WLC、harness 快照。
- `candidates/experiment-01/`：全部八组构建使用的同一完整冻结源。
- `build/timeout-{22,8,4,2}-{off,on-stream}/`：两块板实际 ELF/BIN、`.config`、
  生成 C、compiler SHA、build commands、完整源快照及 Zephyr/70 个 west 项目的实际 HEAD/dirty patch。
- `runs/`：24 个主对照复位 runs + 22 基线后置复测 + 2 最终交付复测，共 **26 个完整通过的 runs**。
  每次保留两方向原始日志、烧录日志、capture/analyzer 源码与 digest、硬件身份、manifest、summary。
- `results.json`、`analyze.py`、`audit.log`：重分析结果与完整配置／源码／依赖／生成 C／编译器匹配核对。
- `driver-source.json`、`zephyr-rx-timeout.patch`、`driver-checkpatch.log`：可复现驱动变更；
  checkpatch **0 errors／0 warnings**。Kconfig style 工具仅报告原文件既有的行 3、11、12 问题。
- `irq-tests.log`：native_sim/native/64，默认关闭与 timing 开启两配置，**25/25 cases 通过**，
  包括有效 callback 计数与已退休 binding 不再计数的回归。
- `harness-tests-final.log`：**21 个测试通过**，含 timeout/config/probe/错误 profile 的拒绝校验。

所有 runs 经严格 analyzer 校验：26,000 个正式 RPC、14,000 个 paced samples、120,000 个
continuous samples 均通过；无 overflow、传输错误、坏帧、样本缺失、重复或乱序。
最终 stricter analyzer 重分析 26 个 runs，summary 与原记录一致。

没有执行其他库、其他平台、CRC32C 或完整 benchmark 矩阵；本轮源代码仅涉及平台
驱动配置、可选 IRQ 诊断和测试工具，不需要改变先前的 frozen-v1、WLC 或 core 验收结论。
