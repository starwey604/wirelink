# H7 帧编码与重试 CPU 微基准

这个独立 Zephyr app 直接链接当前 Wirelink 核心，与
[`benchmarks/framing`](../../framing/README.md) 共用 `workload.c`。
不启动 Willow、电机、USB 通信或 host executor，也不改变产品仓库的依赖。
它补充纯核心的板端 CPU 数据；不是 Willow 整机占用率或端到端延迟测试。

## 构建

需要初始化好的 Zephyr 工作区、SDK，以及 Ragtime_Firmwares 的 `dm_mc02`
板定义。从 Zephyr 工作区运行，按本机位置替换路径：

```sh
west build -s /path/to/wirelink/benchmarks/zephyr/framing \
  -d /path/to/wirelink/build/framing-h7-before -b dm_mc02 -- \
  -DBOARD_ROOT=/path/to/Ragtime_Firmwares/firmware \
  '-DZEPHYR_MODULES=/path/to/wirelink;/path/to/Ragtime_Firmwares/modules/hal/cmsis_6;/path/to/Ragtime_Firmwares/modules/hal/stm32;/path/to/Ragtime_Firmwares/modules/debug/segger'
```

先冻结旧实现的 ELF/HEX 和 SHA-256，再改核心，在另一个构建目录生成 after。
前后保持相同工具链、板配置、LTO、优化选项和 harness。不要修改后再重建 before。
本例启用 `CONFIG_ASSERT`，断言包含测试执行与校验，不要在此基准中关闭它。
本轮冻结镜像实际使用 `-O2`、LTO 关闭、I/D cache 开启。该板默认 ISR table 配置
不满足当前 Zephyr 的 LTO 依赖，不能仅凭 `prj.conf` 请求了 `y` 就宣称启用了 LTO。
如需研究 LTO，必须用满足依赖的相同配置重新生成完整的前后对照，不能混用本轮基线。

## 烧录与采集

J-Link Commander 选择 STM32H723ZG、SWD。仅由 Commander 占用探针；
采集脚本连接它的 RTT TCP 服务，不再启动第二个 J-Link 客户端。
沿用本板已验证的 `exec SetAllowStopMode = 0`；连接不稳时可从 1000 kHz 降到 100 kHz。

1. `h` 停核，`loadfile /absolute/path/zephyr.hex` 烧录，`r` 复位。
2. 从该 ELF 的符号表读取 `_SEGGER_RTT` 地址，使用
   `exec SetRTTAddr 0x...` 指定它；不要沿用其他固件的地址。
3. 先启动采集，看到 `RTT connected` 再在 Commander 输入 `g`：

```sh
python3 benchmarks/zephyr/willow_cpu/capture.py --seconds 30 --out before-rtt.log
```

保留 Commander 连接，依次烧录、采集 after；必要时再做一次 A/B。
看到连接提示后应及时启动 MCU，并等本次采集退出再复位或烧录。若采集混入
复位前 RTT 残留、损坏字节或缺行，应废弃整轮并重新采集，不裁掉坏行保留部分样本。
连接失败时可以降速或 connect-under-reset；克隆探针可能仍需重新插拔并按住
RESET。本轮实测按住时 DAP 初始化成功但 CPU halt 超时，松开后同一连接立即成功；
不要在持续按住时反复等待 CPU 停核。镜像不包含产品应用，测试后须明确记录板上留下的固件。

## 计量与验收

每组先预热 16 次，再测 5 批，每批 32 次操作。DWT 计数覆盖批次循环，
批次内暂时屏蔽中断；批次外恢复中断、校验结果和输出 RTT，每组间隔 20 ms。
读取 `timing_freq_get()` 报告的频率换算，不假设 CPU 频率。
结果是热缓存、短暂排除调度干扰下的 CPU 成本，不能代表最坏中断延迟。
本轮实测最大批次 before 9.193 ms、after 2.690 ms；关中断批次仅用于这个无外围业务的
实验 app，不能搬入产品 owner loop。正式整机验证必须恢复实际调度/中断负载。

矩阵覆盖七种模式、COBS/native、三种数据分布、32/120/512/2048 B；使用产品
当前的 NONE 完整性配置。CRC 和 length16 另在主机完整矩阵验证。
要求完整的 begin、840 条样本、end/pass，缺行或重复复位均视为无效采集。

```sh
python3 benchmarks/framing/compare.py before-rtt.log after-rtt.log
```

报告给出每操作 cycles 和 ns 的重复中位数；不能与主机数据交叉作比。
保留原始 RTT、ELF、哈希和 `.config`，不要仅保存百分比。
首轮 A/B 及其退化见[帧编码性能记录](../../../docs/framing-performance-cn.md#h7-实测)；
后续修正、重新配对验证和当前板状态见
[回退路径收敛](../../../docs/framing-fallback-performance-cn.md)。
