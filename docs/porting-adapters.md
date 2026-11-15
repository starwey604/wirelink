# 传输层移植指南（对接 `wl_packet_port_t`）

[返回文档总览](README.md) · [Wire v2 规范](wire-format.md) · 核心接口：[`packet_port.h`](../include/wirelink/packet_port.h) · [`packet_stream.h`](../include/wirelink/packet_stream.h) · [`byte_fifo.h`](../include/wirelink/byte_fifo.h)

Wirelink 内核不绑定任何操作系统、ISR、DMA 或 Socket API。将自定义硬件驱动（如 STM32 UART DMA、ESP32 SPI 从机、USB Bulk 或私有无线电芯片）接入 Wirelink，只需实现约 **60~120 行**适配器胶水代码。

---

## 一、架构边界与并发铁律

```text
  Hardware / ISR / DMA                    Single Owner Loop / Task
 ========================              ==============================
  RX Done IRQ                           wl_byte_fifo_to_cobs() [若用串口]
    ├─ 包传输: wl_packet_rx_claim          │
    │          memcpy / DMA write          ▼
    │          wl_packet_rx_commit ───► wl_kernel_step(&ep.core, now, 16)
    │                                      ├─ 分发超时/取消/RPC 完成回调
    └─ 流传输: wl_byte_fifo_reserve        ├─ 借用 RX -> route() -> release
               wl_byte_fifo_commit         └─ 调用 config.submit() 发送 TX
                                                │
  TX Done IRQ                                   ▼
    └─ wl_packet_tx_publish(token, flags) ◄── 驱动异步发送完成发布
```

请严格遵守以下两条**并发红线**：

1. **所有内核 API 只属于单一 Owner**：`wl_kernel_step`、`*_start`、`*_send_*`、`wl_kernel_cancel`、`wl_kernel_close` 及生成回调均必须在同一个线程/主循环中顺序调用（或受同一把外层互斥锁保护）。
2. **ISR / I/O 线程禁止直接运行内核**：中断或 DMA 回调中**只能**调用原子边界函数：
   - 接收：原生包调用 `wl_packet_rx_claim` + `wl_packet_rx_commit` / `wl_packet_rx_abort`；字节流调用 `wl_byte_fifo_reserve` + `wl_byte_fifo_commit` + `wl_byte_fifo_note_gap`。
   - 发送完成：`wl_packet_tx_publish(port, token, flags)`。
   - 唤醒通知：`wl_packet_activity(port)`（再触发外层信号量 / EventGroup / Flag）。

---

## 二、模式 A：有消息边界的驱动（USB Bulk / UDP / SPI 定长帧）

适用于物理层本身能交付完整报文（Short Packet / ZLP / UDP Datagram / 定长包）的设备。硬件包直接交给 `wl_packet_port_t` 的双槽原子 RX 环（`WL_PACKET_RX_SLOTS = 2`）。

### 1. 初始化静态存储与端口（`~15` 行）

<!-- [docs:loopback_setup] -->
```c
wl_packet_config_t config = {.instance = 1001U + side,
    .rx_storage = &receive_storage[side][0][0], .rx_unit_size = 256U,
    .rx_storage_size = sizeof(receive_storage[side]),
    .submit = wl_packet_loopback_submit, .notify = wl_packet_loopback_notify,
    .user = &transport.directions[side]};
require(wl_packet_init(&ports[side], &config) == WL_OK);
demo_kernel_handlers_t handlers = {.on_rpc_execute = execute, .on_message_event = received};
require(demo_kernel_init(&endpoints[side], &ports[side], &handlers, NULL, NULL) == WL_OK);
```
<!-- [/docs:loopback_setup] -->

关键参数要求：
- `instance`：**必须非零**，每次重启/重新初始化链路时递增或更换新值，防止旧中断携带过期 token 污染当前会话。
- `rx_storage_size`：必须等于 `2 * rx_unit_size`，且对原生 USB/UDP 建议 `rx_unit_size >= 最大规范帧 + 1`（预留 1 字节以检测超长截断包）。

### 2. 实现 TX `submit` 回调与异步完成发布

`wl_packet_sink_fn` 在 Owner 上下文内由 `wl_kernel_step` 同步调用，驱动需按实际异步能力返回四个枚举值之一：

| 返回值 | 含义 | 驱动后续义务 |
| --- | --- | --- |
| `WL_PACKET_SENT` | 在当前调用返回前已消费完 `data` 且物理发送已完成 | 无需调用 `wl_packet_tx_publish` |
| `WL_PACKET_STARTED` | 已接管 `data`（指针在发送完成前保持稳定），正在 DMA/外设发送 | 后续必须携带传入的 `token` 调用 `wl_packet_tx_publish` |
| `WL_PACKET_BUSY` | 驱动硬件忙碌，本次**未接收**任何字节 | 硬件空闲时必须调用 `wl_packet_activity(port)` 唤醒 Owner 重试 |
| `WL_PACKET_FAILED` | 确定性发送失败，本次未接收 | 内核向相关请求标记本地 I/O 错误，无后续 completion |

异步完成后发布 flags 规则：
- `WL_PACKET_CONSUMED`：物理外设/DMA 已读取完毕 `data` 内存（例如字节已全部拷入驱动 FIFO），但总线尚未 idle。
- `WL_PACKET_DRAINED`：物理总线传输彻底完成（**隐含 `CONSUMED`**，通知 `wl_kernel` 释放该 TX 租约槽位）。
- `WL_PACKET_TX_ERROR`：传输中止或失败（**隐含 `CONSUMED | DRAINED`**）。

### 3. ISR / RX Producer 零拷贝接收模板

```c
/* 在硬件收到一包完整数据时由 RX 中断或接收线程调用（串行生产者） */
static void on_hw_packet_received(wl_packet_port_t *port,
                                  const uint8_t *hw_buf, size_t len) {
  wl_packet_claim_t claim;
  if (len == 0U || wl_packet_rx_claim(port, len, &claim) != WL_OK) {
    return; /* RX 双槽暂满或长度非法：丢弃或触发流控 */
  }
  memcpy(claim.span.data, hw_buf, len);
  wl_packet_rx_commit(port, &claim, len); /* 内部自动触发 config.notify(user) */
}
```

若底层 DMA 可直接把收包写到指定地址，也可提前 `wl_packet_rx_claim` 并将 `claim.span.data` 交给 DMA 描述符；收到损坏包/错误中断时调用 `wl_packet_rx_abort(port, &claim)`。

---

## 三、模式 B：字节流驱动（UART DMA / UART IRQ / CDC ACM）

串口字节流没有物理包边界，推荐在 `wl_packet_port_t` 下挂接 `wl_byte_fifo_t` + `wl_packet_stream_t`（参考完整 C 实现 [`examples/common/packet_tool_owner.c`](../examples/common/packet_tool_owner.c)）：

```c
/* 1. 静态声明串口所需对象（全部可放静态 BSS） */
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static wl_byte_fifo_t rx_fifo;
static uint8_t port_rx[2][2064];
static uint8_t stream_tx[2064]; /* >= wl_packet_stream_cobs_crc16_bound(max_frame) */
static uint8_t fifo_buf[1024];

/* 2. 初始化链路 */
wl_packet_config_t pcfg = {
    .instance = next_instance(),
    .rx_storage = &port_rx[0][0],
    .rx_unit_size = sizeof(port_rx[0]),
    .rx_storage_size = sizeof(port_rx),
    .submit = wl_packet_stream_cobs_crc16_submit,
    .notify = wl_packet_stream_notify,
    .user = &stream,
};
wl_packet_init(&port, &pcfg);

wl_packet_stream_config_t scfg = {
    .port = &port,
    .tx_storage = {stream_tx, sizeof(stream_tx)},
    .maximum_frame_size = APP_KERNEL_WIRE_FRAME_MAX,
    .physical_submit = my_uart_dma_submit, /* 只发不打包 */
    .notify = my_wakeup_owner,
    .user = &my_uart_ctx,
};
wl_packet_stream_cobs_crc16_init(&stream, &scfg);
wl_byte_fifo_init(&rx_fifo, (wl_span_t){fifo_buf, sizeof(fifo_buf)},
                  my_wakeup_owner, &my_uart_ctx);

/* 3. UART RX ISR / DMA 半满中断内调用 */
static void on_uart_rx_bytes(const uint8_t *bytes, size_t len) {
  wl_span_t dst;
  size_t off = 0U;
  while (off < len && wl_byte_fifo_reserve(&rx_fifo, &dst) == WL_OK) {
    size_t n = len - off;
    if (n > dst.length) n = dst.length;
    memcpy(dst.data, bytes + off, n);
    wl_byte_fifo_commit(&rx_fifo, n);
    off += n;
  }
  if (off < len) wl_byte_fifo_note_gap(&rx_fifo); /* FIFO 满丢字节时显式报错重同步 */
}
```

然后在 Owner 主循环每次 `step` 前，调用 `wl_byte_fifo_to_cobs(&rx_fifo, &stream, 16U)` 将 FIFO 中的原始串口字节解码为完整规范包提交给 `port`。

---

## 四、Owner 调度循环、低功耗等待与安全关闭

为了在 RTOS/Linux 事件循环中实现**零轮询、零唤醒丢失**的低功耗等待，推荐采用“先 `service` + `step`、再检查 `wl_kernel_ready`”的闭环模型（下段来自可编译验证的 Host UDP 示例 [`examples/common/tutorial_host.cpp`](../examples/common/tutorial_host.cpp) 与 [`examples/05_zephyr_udp/server.c`](../examples/05_zephyr_udp/server.c)）：

<!-- [docs:udp_host_step] -->
```cpp
wl_err_t example_udp_step(example_udp_t *udp, wl_kernel_t *core, uint32_t now) {
  try {
    const auto error = udp->adapter->service();
    const auto advanced = wl_kernel_step(core, now, 16);
    return error == WL_OK ? advanced : error;
  } catch (...) { return WL_ERR_IO; }
}
```
<!-- [/docs:udp_host_step] -->

<!-- [docs:udp_server_loop] -->
```c
while (example_running()) {
  uint32_t now = example_now_ms();
  if (example_udp_step(udp, &server.core, now) != WL_OK) { status = 1; break; }
  if ((int32_t)(now - due) >= 0) {
    telemetry_t sample = {.has_sample = true, .sample = ++sequence};
    const wl_err_t error = udp_demo_kernel_send_telemetry(&server, &sample);
    if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) { status = 1; break; }
    due = now + 20;
  }
  now = example_now_ms();
  uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
  if (example_udp_wait(udp, &server.core, wait) != WL_OK) { status = 1; break; }
}
```
<!-- [/docs:udp_server_loop] -->

### 安全关闭与静态存储回收顺序

1. Owner 调用 `wl_kernel_close(&endpoint.core)`（进入关闭态，拒绝新调用并将待完成本地调用转入 `WL_KERNEL_CLOSED`）。
2. 停止底层物理驱动，确保正在进行的异步 RX/TX 归还缓冲区并发布最终 completion。
3. Owner 继续调用 `wl_kernel_step(&endpoint.core, now, 16)`，直至 `wl_kernel_quiescent(&endpoint.core)` 返回非零，此时所有回调与物理租约已彻底清零，可安全释放或以新 `instance` 重新初始化静态区。

> 参考仓库现成适配器实现：
> - Zephyr UART DMA / IRQ：`adapters/zephyr/uart_dma/`、`adapters/zephyr/uart_irq/`
> - Zephyr USB Bulk：`adapters/zephyr/usb_bulk/`
> - Asio UDP / Astrial 串口与 USB：`adapters/asio_udp/`、`adapters/astrial/`、`adapters/astrial_usb/`
