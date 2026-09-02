# Adapter HIL 验收标准

Wirelink 的 hosted adapter 通过 `wl_adapter_stats_t` 暴露统一的验收视图，
产品测试不再依赖 Astrial Serial、USB Bulk 或 Asio UDP 的私有计数字段。
adapter-specific stats 仍可用于驱动诊断，但不作为跨平台通过条件。

## 通用通过条件

在测试开始前记录一次快照，结束并 drain owner pump 后再记录一次；所有判断
使用两次快照的差值：

- `errors == 0`；
- `rx_units` 与对端成功发送的 transmission unit 数一致，`rx_bytes` 与线上的
  实际字节数一致；
- `tx_units == tx_completions`，且 `tx_bytes` 与已提交的 transmission unit
  总字节数一致；
- 稳态结束时 `tx_active == 0`；若 owner 已释放全部 RX event，
  `rx_paused == 0`；
- event-driven Serial/USB 路径的 `activity_notifications > 0`；同步 UDP 路径
  则要求 `service_calls > 0`，并验证 1 ms 默认 deadline 能唤醒 owner；
- 压力测试允许 `rx_backpressure > 0`，但 release 后必须恢复接收，且可靠包
  最终由重传收敛；不允许覆盖借用中的 RX 数据。

## 分层执行

1. 每次提交先运行 PTY serial、UDP loopback、host executor，以及
   `native_sim`/QEMU application runtime；这些测试不依赖设备。
2. UART HIL 使用 `samples/zephyr/uart_dma` 的独立 TX/RX 接线，检查持续流量、
   背压恢复及 adapter errors。
3. USB HIL 烧录 `samples/zephyr/usb_bulk`，host 运行
   `wirelink_transport_benchmark --transport bulk`。性能结果按
   `docs/usb-performance.md` 留存，同时应用上面的通用正确性条件。
4. 产品级 libflorid/固件回环应额外验证 typed RPC、LATEST 控制帧、停止时
   quiesce，以及断连后不再访问 `wl_ctx_t` 存储。

HIL 结果必须记录 commit、板型、传输配置、迭代数、payload、延迟分位数、
CPU 指标及两端统计快照。没有连接硬件时，只能报告 host/QEMU 通过，不能把
它写成实板通过。
