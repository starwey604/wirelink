# Adapter 硬件验证清单

[Adapter 合同](adapters-cn.md) · [开发检查](development-cn.md)

验证前记录源码、toolchain、schema/profile、connector、设备标识、driver capability、observer 开关，以及实际 ELF/bin/config/map。受控驱动测试、固件构建和真实硬件运行分别记录。

## 检查真实所有权边界

- 比对实际发送/接收的 unit、字节、错误和残留物理 TX。允许受控背压，恢复后不得覆盖借用 RX；区分暂时存储压力与真实 overrun/丢字节。
- TX 源在 CONSUMED 前不变，DRAINED 被 owner 取得前不能启动下一笔物理传输。timeout/cancel 不得提前释放驱动借用；成功 drain 和停止取消以不同结果记录。
- 覆盖内联/延迟 completion、过期 token、满 RX、BUSY 后资源唤醒，以及 stop 期间实际 buffer 归还、callback/worker 退出。关闭后同时确认 kernel quiescence 和 adapter 静默。
- USB native/NONE 检查严格 read headroom、actual MPS、exact-MPS/ZLP、成功空读、full/error 边界故障。fault recovery 必须执行真实 reset/reconfigure；Zephyr `recover` 保持 kernel instance/工作，完整 teardown/restart 则用新 instance。
- UART DMA 检查真实 RX buffer 归还与 TX_DONE 的驱动含义，需要时启用物理 idle 查询；IRQ 检查 `irq_tx_complete` 能力，poll/host serial 检查实际 `tx_idle`/`drained` provider。记录配置的 drain 检查周期及其单位。缺失所需能力的 CDC 路径应明确拒绝。
- UDP 记录固定/学习 peer、datagram 上限及截断拒绝；本地 socket send 接纳不等于远端收到。验证 query/wait 间到达的通知和 stop 对 wait 的打断。
- 丢请求/丢响应后，应用可以显式重试，重复请求可能再次执行；需要避免重复副作用时验证应用幂等键或 job 协议。

## 记录测量所代表的工作

吞吐量注明计数边界和有效 payload 字节；延迟注明起止事件、tick 单位及分位数。CPU 指标注明线程/ISR、计时源、采样窗口及 instrumentation 开销。保留压力、故障和关闭结果，不以吞吐量正常代替所有权验收。

Transport 工具见 [benchmarks/host](../benchmarks/host/README.md)，板卡配置见 [USB sample](../samples/zephyr/usb_bulk/README.md)及对应 UART 指南。受控 fixture 的通过证明所模拟边界，不代替实际驱动/板卡资格。
