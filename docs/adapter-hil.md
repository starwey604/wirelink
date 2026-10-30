# Adapter HIL 验证范围（0.13）

当前 adapter 通过各自的公开 counter/状态和 packet port 所有权验收，不再存在旧 wl_adapter_stats_t 通用视图。先记录源码/toolchain/schema/profile、设备标识、observer 开关、ELF/bin/config/map 和实际驱动能力，再采集。软件受控边界测试、固件构建和真实硬件运行分别记录。

- 比对实际发送/接收单元与字节、错误和残留物理 TX；允许受控背压，恢复后不能覆盖借用中的 RX。
- TX 源在 CONSUMED 前不变，DRAINED 前不能启动下一物理传输；timeout/cancel 不能提前释放驱动借用。
- 覆盖 callback inline/延迟、旧 token、满 RX、BUSY 唤醒和 stop 期间实际 buffer/callback 归还。关闭后确认 host I/O 已退出。
- USB 使用 native/NONE，检查读 headroom、exact-MPS/ZLP、成功空读及 full/error 边界故障；恢复必须真实 reset/reconfigure 和新 instance。
- UART DMA 检查真实 buffer 归还，IRQ 检查 TX-complete API，poll/host serial 检查真实物理 idle query。当前 CDC 不满足 TX-complete，是明确拒绝探针，不是通过的 echo 路径。
- 应用丢请求/丢响应后可显式重试，重复请求可能再次执行；不能沿用旧 ARQ 自动收敛或 exactly-once 断言。

当前 raw/object 工具见 [benchmarks/host](../benchmarks/host/README.md)，真实 USB sample 见 samples/zephyr/usb_bulk。软件 object 测试验证应用 duplicate/commit，不代替物理 USB 资格。K4 封存对照是特定源/环境下的研究证据，K5 后需最终构建确认；不自动把迁移前数字当作最终产品数字。

[Adapter 合同](adapters-cn.md) · [迁移](migration-0.13-cn.md) · [开发检查](development-current.md)。
