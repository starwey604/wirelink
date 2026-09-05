# Adapter 生命周期

> 英文版 [`adapters.md`](adapters.md) 是规范来源。

即使平台 API 不同，Wirelink adapter 也遵循同一生命周期：

1. 初始化 `wl_ctx_t` 和全部调用方存储。
2. 在 driver 停止时初始化 adapter，并绑定 TX sink。
3. 激活 RX 或启用平台 transport。
4. 每次 RX、TX completion、writable 或 deadline 唤醒后，由单一 owner 调用
   `wl_poll()`，再运行 adapter/runtime service。
5. 复用 link 或 adapter 存储前，停止 producer、排空延迟 completion 并 quiesce。

`WL_ERR_WOULD_BLOCK` 表示背压，不是 adapter 的终态失败。等待下一次 transport
通知，再让 owner 执行一次 service pass。

## 平台映射

| Adapter | Envelope | 激活 | Owner service | Quiesce |
| --- | --- | --- | --- | --- |
| Loopback | native packet | `wl_loopback_init()` | `wl_loopback_service()` | `wl_loopback_quiesce()` |
| Zephyr UART DMA | COBS stream | `wl_zephyr_uart_dma_start()` | `wl_zephyr_uart_dma_service()` | 调用 `wl_zephyr_uart_dma_stop()`，service 到 `started=0` 且 `stopping=0` |
| Zephyr UART IRQ / CDC ACM | COBS stream | `wl_zephyr_uart_irq_start()` | `wl_zephyr_uart_irq_service()` | 复用 link 前禁用所属 UART/USB 设备 |
| Zephyr USB bulk | native packet 或 COBS stream | 启用所属 USBD context | `wl_zephyr_usb_bulk_service()` | 复用 link 前禁用 USBD |
| Astrial serial / USB | COBS stream / native packet | `start()` | `service()`，可用时配合 `wait_for_activity()` | `quiesce()` |
| Asio UDP | native packet | 构造并 bind socket | `service()` | `quiesce()` |

类型化 adapter 暴露平台专用配置与统计；C core 不增加虚调用，也不拥有等待原语。

## Owner Loop 模式

把 `service` 和 `quiesce` 填入 `wl_pump_hooks_t`，分别接入 adapter 与生成
runtime 的 deadline hint，再用 `wl_pump_get_hint()` 决定下一次等待时间。每次
wake 都先运行一次 `wl_pump_step()` 再查询新 hint，避免 sink 背压演变为零延时忙循环。

每条 link 只能选择一个 ingress：复制式 stream 用 `wl_feed_bytes()`，直接 stream
用 reserve/commit 或 DMA claim，native packet 用 unit queue。同一已初始化 context
不得混用 ingress family。
