# H7 CPU 与 ArmProtocol 临界区测量

这是一个**只用于测试**的 Zephyr HIL 包装应用。它直接编译现有 Ragtime HIL/ArmProtocol 源码，
不复制产品实现、不改产品文件或依赖 pin。没有 CAN、电机、NVS 和看门狗业务。

产品当前要求 WLC ABI 26。本应用使用产品现有配套源码；它不是 Wirelink ABI 28 的板端验收。
构建时保留原 ABI 断言，不能通过关闭断言或伪造宏来迁移版本。

## 构建与采集

```sh
export ZEPHYR_SDK_INSTALL_DIR=/absolute/path/to/zephyr-sdk
python benchmarks/zephyr/willow_cpu/build.py \
  --workspace /absolute/path/to/Ragtime_Firmwares \
  --wlc /absolute/path/to/paired-abi26-wlc \
  --build-dir build/willow-cpu-on --lock-profile on --period-us 2000
```

使用 `--lock-profile off` 构建对照；使用 `--period-us 1000` 测 1 kHz。
每个变体用独立构建目录。脚本调用工作区 `.venv/bin/west`，只加载测试所需的产品/Zephyr模块。

烧录后用同一个 Commander 的 RTT TCP 端口采集，不同时开第二个 probe 会话：

```sh
python benchmarks/zephyr/willow_cpu/capture.py --seconds 120 --out h7-rtt.log
```

地址应先从当前 ELF 的 `_SEGGER_RTT` 符号确认。本轮为 `0x24000000`，Commander 中可用
`exec SetRTTAddr 0x24000000` 避免轮询扫描整个 RAM，见 [SEGGER 说明](https://kb.segger.com/RTT)。
启用 `exec SetAllowStopMode = 0`，保持采样期间目标运行；采样后再执行调试/停机命令。

启动产品已有 `willow_wirelink_hil_host`，将 `--period-us` 与镜像一致，保留严格门槛：

```sh
willow_wirelink_hil_host --vid 2fe3 --pid 574c \
  --period-us 2000 --warmup 500 --warmup-idle-ms 600 \
  --samples 5000 --cycles 3 --reopen-delay-ms 100 \
  --strict-performance --require-diagnostics
python benchmarks/zephyr/willow_cpu/analyze.py h7-rtt.log > h7-summary.json
python benchmarks/zephyr/willow_cpu/test_analyze.py
```

分析器从 HIL 最终记录推导有效命令窗口，只选完全落在窗口内的 2 秒 CPU 区间，
排除 warmup、收尾 idle 和跨边界区间。缺失配对计数或没有有效窗口时报错。
同次启动的分段采集可以依次传入多个文件；不能混合重启前后的计数。

## 测量边界

- `wl_h7_cpu_v1` 使用 Zephyr 的线程/系统 runtime stats 和 Cortex-M DWT，
  [统计机制](https://docs.zephyrproject.org/latest/kernel/services/threads/index.html#runtime-statistics)
  区别于 owner pass 起止墙时。百分比用 DWT 频率乘窗口墙时作分母，另保留总记账周期以供核对。
  ISR 归属遵从 Zephyr 架构的调度记账，**不是排除中断的纯函数指令耗时**。
- `wl_h7_lock_v1` 只在 ArmProtocol 编译目标拦截 `k_spin_lock/unlock`；不测 kernel、
  USB 或 HIL 自身的其他锁。无 SMP，进入锁后关中断，计数不需要另加锁或 64 位原子。
- 等待项包含读钟、关中断及此前可能发生的抢占；单核上不能把它直接称为“自旋争用”。
  持有区间从获取锁后的读钟到 unlock 探针入口，包含获取探针的内部记账，
  不包含 unlock 探针的计数收尾和实际解锁；不是完整 IRQ-off 时间的上限。
- `waitmax/holdmax` 是启动以来最大值，不是当前区间最大值或 p99。
  ON/OFF 均开启 CPU 记账、RTT 和断言，OFF 只关闭锁探针，并非完全未插桩的生产固件。
- 不在热路径打印，低优先级报告线程每 2 秒输出；RTT 为非阻塞丢弃模式。
  数据不覆盖完整 Willow 电机/CAN/持久化负载，也不能据此改变那些业务的并发合同。

实测及保留/改锁决定见 [性能记录](../../../docs/executor-h7-performance-cn.md)。
