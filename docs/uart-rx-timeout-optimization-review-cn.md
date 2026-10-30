# UART RX timeout 优化审核

日期：2026-10-14。对应[优化报告](uart-rx-timeout-optimization-cn.md)。

**当前 ESP32-S3、1 Mbaud、8N1、timeout=2 的优化效果通过复核；配置功能还存在一项 P2 边界问题，关闭后才能将本次交付整体标记为通过。**

后续实施状态：R1 已在第二轮 P0 修复，285 个边界用例及推荐 2/默认 22 真板复测通过，
见[修复后阶段记录](codec-rpc-refactor-round-2-result-cn.md)。以下保留原审核发现与历史证据。

本次没有改生产代码或重新烧录。独立证据在 [`review-20261014`](../../wirelink-uart-timeout-20261014/review-20261014/)。

## 1. R1 / P2：配置上界超过真实硬件容量

位置：[驱动补丁](../adapters/zephyr/patches/esp32-uart-rx-timeout.patch)第 13、32 行；[benchmark builder](../../wirelink-cross-benchmark/scripts/build-esp32s3.py)第 91 行。

Kconfig 和 builder 都允许 1..126 字符，但驱动直接调用 `uart_hal_set_rx_timeout()`，没有验证当前 UART 格式与硬件的最大阈值。

实际依赖中的调用链：

1. `esp_hal_uart/uart_hal.c` 的 `uart_hal_set_rx_timeout()` 把字符数乘以 `uart_hal_get_symb_len()`，转为 bit-period threshold。
2. `esp_hal_uart/esp32s3/include/hal/uart_ll.h` 的 `uart_ll_set_rx_tout()` 直接赋值给 `mem_conf.rx_tout_thrhd`。
3. ESP32-S3 的 `soc/uart_struct.h` 将该字段定义为 **10 bits**，最大 1023；HAL 也提供 `uart_hal_get_max_rx_timeout_thrd()` 查询当前格式的最大字符数。

8N1 每字符 10 bits，故最多 102 字符；8E2 等 12-bit 格式最多 85 字符。超出后发生截断：

| 格式 | 配置字符数 | 请求 bit 周期 | 实际写入 bit 周期 |
|---|---:|---:|---:|
| 8N1 | 102 | 1020 | 1020 |
| 8N1 | 103 | 1030 | **6** |
| 8N1 | 126 | 1260 | **236** |
| 12 bits/字符 | 86 | 1032 | **8** |

用当前依赖的真实寄存器类型构建了[独立反例](../../wirelink-uart-timeout-20261014/review-20261014/timeout-register.c)，上述截断已复现，见[输出](../../wirelink-uart-timeout-20261014/review-20261014/timeout-register.log)。这是寄存器写入的确定性复现，不是重新测量物理 UART timeout。

影响：合法配置可能得到远短于声明的等待时间，增加间歇流量的中断和 CPU；`.config` 与日志仍然显示请求值，现有 analyzer 不能识别实际寄存器截断。**已测 2/4/8/22 都在容量内，性能结果不受此问题影响。**

修复要求：

- 按实际 SoC、时钟配置和 UART 字符格式核验阈值；优先使用上述 HAL 查询，在写入前拒绝不能表示的值，并返回明确错误。不能静默截断或静默钳位。
- 若选择收窄 Kconfig 范围，仍要覆盖运行时 `uart_configure()` 改变字符格式后的容量；仅把 126 改为 102 不够。
- 同步补丁、已应用的 Zephyr 源码、builder 校验、文档；builder 的固定 ESP32-S3/8N1 场景应拒绝 >102，驱动仍承担通用校验。
- 覆盖 8N1 的 102/103、12-bit 格式的 85/86、默认 22 和推荐 2。确认失败不能让后续 RX 带着被截断的阈值运行。
- 保存新的 source/patch manifest。若驱动二进制有变化，重跑推荐 2 与默认 22 的交付 smoke；本次四档数据保留为历史证据。

## 2. 已通过的独立复核

使用**当前严格 analyzer**重分析 26 份保存的 capture，全部通过，summary 与原记录逐份一致：26,000 次正式 RPC、14,000 paced samples、120,000 continuous samples。全部实际 ELF/BIN、生成文件与 `.config` 的已记录哈希复核通过。

对八组构建的两端执行 16 项配置/输入检查：同一测量集合中完整 Kconfig 仅 timeout 不同；源码、70 个 west 依赖、WLC、生成代码及编译器 SHA 一致。生产源码与测量冻结源一致，后续差异是文档、测试和 analyzer 的额外 profile 校验；WLC 没有变化。

| 无探针，三轮分位数中位数 | timeout=22 | timeout=2 | 减少 |
|---|---:|---:|---:|
| RTT p50 µs | 1216.821 | 817.329 | 399.492 / 32.83% |
| RTT p99 µs | 1248.767 | 848.767 | 400.000 / 32.03% |

后置 22 复测和最终 2 交付复测一致。每条 RPC 的实际线字节没有改变，均值 53.056 B；收益与双向 timeout 缩短的预算吻合，支持此前对 FIFO 尾部等待的判断。

独立 CPU/IRQ 组中每操作 active CPU 和 callback 数基本不变，连续 admission 吞吐约 3571.65 帧/s。更高 RPC/s 带来的 CPU 百分比和每秒 callback 增加不能描述为每 RPC CPU 回退。连续吞吐包含 memory-consumed 背压，尚不包含最后一个字节物理发送完成。

重新运行当前源码的 UART IRQ suite：默认和 timing 两配置，native_sim/native/64，**25/25 cases 通过**。重新运行 harness Python suite：**21/21 通过**。这些测试尚未覆盖 R1 的硬件配置上界。

证据：[复算与控制变量核对](../../wirelink-uart-timeout-20261014/review-20261014/verification.json)、[测试摘要](../../wirelink-uart-timeout-20261014/review-20261014/tests.json)。

## 3. 下一轮基线

推荐保留 timeout=2，默认仍为 22，继续披露全 UART 实例作用范围及本地 Zephyr 补丁依赖。没有证据要求撤销已经测得的延迟收益。

第二轮以 **R1 修复后的当前执行路径 + 原 IRAM 策略 + timeout=2** 冻结并重建 baseline；不能沿用旧 timeout=22 的约 1225 µs 作为新重构的性能分母。历史 817.329/848.767 µs 只作校准，正式比较必须重建新基线。

已准备[第二轮方案](codec-rpc-refactor-round-2-plan-cn.md)，其 P0 明确要求先关闭 R1。本次没有发现其他阻塞问题；没有重跑其他 MCU、CRC32C、间歇字节流、长时间 soak 或完整跨库矩阵。
