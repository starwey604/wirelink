# 接入同步与异步物理链路

[文档总览](README.md) · [节点与生成 API](../wlc/README.md) · [packet_port.h](../include/wirelink/packet_port.h) · [packet_stream.h](../include/wirelink/packet_stream.h)

Wirelink 核心不读取时钟，也不运行线程、ISR、DMA 或 socket。节点内置端口、默认双 RX 槽和所选流连接器。同步发送使用 `<node>_bind_sync` 绑定整帧写函数。异步驱动使用 `<node>_bind` 绑定提交和唤醒回调，USB Bulk 可指定满足硬件步长要求的外部 RX 槽。

## 一、同步整帧收发

以下用 [遥测示例](../examples/00_telemetry/) 的 `pub` 和 `sub` 演示进程内回环。先生成两个节点：

```sh
wlc compile examples/00_telemetry/telemetry.wl --all-nodes --out-dir build/telemetry
```

### 初始化与 native 接收

```c
#include "telemetry.h"
static pub_node_t publisher;
static sub_node_t subscriber;

static wl_err_t pub_tx_write(pub_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return wl_node_rx_packet(&subscriber.node, data, length);
}
```

调用 `pub_bind_sync(&publisher, pub_tx_write)` 和 `sub_bind_sync(&subscriber)` 并检查返回值。提交 `pub_send_telemetry()` 后，owner 分别调用 `wl_node_step(&publisher.node, now)` 和 `wl_node_step(&subscriber.node, now)`。应用实现 `sub_on_telemetry()` 接收业务值。实际驱动把完整包交给 `wl_node_rx_packet()`，返回成功表示完整包已复制到节点 RX 槽，回调随后由 owner 执行。

默认初始化通过进程级 `wl_packet_instance_next()` 分配非零、不会回绕的实例号，分配失败会传递给调用者。它保证本进程内自动分配之间唯一。链路可能残留重启前旧包时，用 `pub_bind_sync_instance(&publisher, id, pub_tx_write)` 接入应用的持久会话身份策略，并避免显式 ID 与自动 ID 冲突。

### TX 是整帧事务，忙后必须通知恢复

owner 步进时调用初始化中传入的 `tx_write(self, bytes, length)`：

- 返回 `WL_OK`：已在返回前同步消费并完成整帧物理发送，之后不再保留此指针。
- 返回 `WL_ERR_WOULD_BLOCK` 或 `WL_ERR_BUSY`：没有接受任何字节。待硬件或对端 RX 槽恢复可用后，调用 `wl_node_tx_ready(&self->node)`，让 owner 重试。
- 其他错误：发送失败，按节点错误/调用完成路径报告。

如果物理设备需要部分写入或异步持有指针，使用 `<node>_bind` 绑定驱动，由驱动管理物理租约。

回环中的可写条件是对端 RX 有空槽：owner 消费完接收包后通知发送端 `tx_ready()`。单次普通消息提交成功不等于槽位已可复用；后续提交背压时保留业务值，待步进/恢复事件后再试。

### 流接收必须保留 consumed 之外的尾部

在节点中选择 `framing = cobs_crc16` 或 `framing = length16` 后，用以下入口喂入字节流：

```c
size_t consumed = 0;
wl_err_t error = wl_node_rx_feed(&sensor.node, bytes, length, &consumed);
```

这里假定节点名为 `sensor`。返回值 `error` 是 `wl_err_t` 状态，`consumed` 输出已消费字节数。无论返回成功、背压还是坏帧，都按 `consumed` 推进输入；只重试 `bytes + consumed` 之后的尾部。RX 暂满时等待 owner 释放槽位再继续，不能丢尾部、重复喂入前缀或原地忙循环。`rx_feed` 的生产者必须串行；若 ISR 无法保留尾部，先通过 SPSC 字节 FIFO 交给 owner，再由 owner 喂入。字节确实丢失时应走所用连接器的断流/重同步路径，不能当作成功接收。

### 关闭顺序

1. 停止并确认 RX 生产者不再调用 `rx_packet()` / `rx_feed()`。
2. owner 调用 `wl_node_close(&self->node, now)`。流式节点会撤销尚未完成的半帧 RX claim。
3. 遇到 `WL_ERR_BUSY` 或 `WL_ERR_WOULD_BLOCK` 时重新采时并重试关闭，直到 `wl_node_quiescent(&self->node)` 为真，再释放或重新初始化节点存储。

## 二、绑定异步驱动与物理租约

应用先准备驱动，再调用生成的 `<node>_bind`。绑定期间，节点通过 `attach` 向驱动提供已初始化的端口、流和收发上限。动态处理器节点在绑定时追加回调表与用户指针。下段来自 [裸机回环](../examples/bare_metal_loopback.c)。

<!-- [docs:loopback_setup] -->
```c
wl_transport_t *link = wl_packet_loopback_transport(&transport, side);
demo_handlers_t handlers = {.on_execute = execute, .on_event = received};
require(demo_bind_instance(&endpoints[side], 1001U + side, link, &handlers, NULL) == WL_OK);
```
<!-- [/docs:loopback_setup] -->

`instance` 为零时自动分配实例号。默认两个 RX 槽的单槽大小为 `<N>_RX_WIRE_MAX + 1U`，多出的字节用于识别超长报文。USB Bulk 等硬件需要特定步长或对齐时，可通过驱动的 `configure_rx` 回调同时填写 `rx_storage`、`rx_storage_size` 和 `rx_unit_size`。每槽至少为 `<N>_RX_WIRE_MAX + 1U`，总容量至少为 `WL_PACKET_RX_SLOTS * rx_unit_size`。

### 异步 TX：submit 与 completion

owner 调用 transport 的 `submit` 回调。驱动返回值决定是否持有传入的数据指针和 token：

| 返回值 | 接受状态 | 后续义务 |
| --- | --- | --- |
| `WL_PACKET_SENT` | 返回前已消费整帧，物理发送也已完成 | 无 completion |
| `WL_PACKET_STARTED` | 已接受，DMA/设备仍持有稳定指针 | 携带原 token 发布最终 completion |
| `WL_PACKET_BUSY` | 未接受任何字节 | 资源恢复时 `wl_packet_activity(port)` 唤醒 owner |
| `WL_PACKET_FAILED` | 未接受，确定发送失败 | 无 completion；相关请求报告本地 I/O 错误 |

异步驱动用 `wl_packet_tx_publish(port, token, flags)` 发布状态：

- `WL_PACKET_CONSUMED`：外设已不再读取源缓冲，但物理总线可能仍忙。
- `WL_PACKET_DRAINED`：物理发送完成，隐含 `CONSUMED`，允许释放租约。
- `WL_PACKET_TX_ERROR`：传输失败，隐含 `CONSUMED | DRAINED`。

业务 RPC 超时或取消不会使仍在 DMA 使用中的缓冲提前失效。节点与驱动存储必须保留到实际租约归还。

### RX：完整包或单生产者字节 FIFO

完整包驱动可直接使用 `wl_packet_rx_push(port, bytes, length)` 复制一包，并检查返回值。真正零拷贝时先 `wl_packet_rx_claim()`，把 `claim.span.data` 交给 DMA，完成后 `wl_packet_rx_commit()`；失败时 `wl_packet_rx_abort()`。双槽 RX 暂满时，由硬件流控或应用丢包策略处理，不能覆盖已提交的槽。

UART 等流式链路使用节点内的 `wl_packet_stream_t`，可按需增加 `wl_byte_fifo_t` 隔离 ISR 与 owner。参考 [Zephyr UART 适配器](../adapters/zephyr/)：

- 初始化按节点的 TX 和 RX 规范帧上界配置连接器，并分配 `<N>_TX_WIRE_MAX` 大小的 TX 存储。
- ISR 使用 `wl_byte_fifo_reserve()` / `commit()`；FIFO 满导致字节丢失时调用 `wl_byte_fifo_note_gap()`。
- owner 用 `wl_byte_fifo_to_cobs()` 排入 stream；直接调用 stream `feed()` 时也必须处理 `consumed` 尾部。

### 单 owner 与安全关闭

所有节点调用、发送、步进、取消、关闭及业务回调都在同一 owner 中顺序运行。ISR / I/O 线程只能使用传输层允许的单生产者/原子入口：RX claim/commit/abort/push、FIFO reserve/commit/note_gap、TX publish 与 activity 通知；不能运行内核。

异步驱动按以下顺序关闭：

1. owner 调用 `wl_node_close(&self->node, now)` 停止新提交并请求驱动停机。
2. 若返回 `WL_ERR_WOULD_BLOCK`，驱动仍在停机；若返回 `WL_ERR_BUSY`，节点仍在排空。保持缓冲有效，待进展后用最新时间重试关闭。
3. 硬件错误传给调用方处理。关闭返回 `WL_OK` 后，驱动已停止，半帧 RX、物理租约与终态回调均已清理，可以释放或重用存储。

## 三、Host / RTOS 事件循环

桌面 UDP 程序包含 `<wirelink/udp.h>` 并链接 `Wirelink::udp`。CMake 工程启用 C 和 CXX，应用源文件按 C11 编译，由 C++ 链接器链接 Asio 库。

调用 `wl_udp_open(&udp, &config, &os_error)` 绑定套接字，再通过 `<node>_bind(&self, wl_udp_transport(udp))` 启动节点 I/O。默认收发上限来自所绑定的节点。owner 在 `wl_node_step` 后重新采时，再调用 `wl_node_wait`；节点关闭成功后调用 `wl_udp_close`。以下循环来自 [Host UDP 示例](../examples/05_zephyr_udp/)。

<!-- [docs:udp_server_loop] -->
```c
while (example_running()) {
  uint32_t now = example_now_ms();
  if (wl_node_step(&server.node, now) != WL_OK) { status = 1; break; }
  if ((int32_t)(now - due) >= 0) {
    telemetry_t sample = {.sample = ++sequence};
    const wl_err_t error = udp_server_send_telemetry(&server, &sample);
    if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) { status = 1; break; }
    due = now + 20;
  }
  now = example_now_ms();
  uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
  if (wl_node_wait(&server.node, now, wait) != WL_OK) { status = 1; break; }
}
```
<!-- [/docs:udp_server_loop] -->

简化 RPC 从下一次 owner step 才开始超时；`_ex` 和 SDK 使用显式时刻，排队时间计入预算。恢复可写、RX 到达、关闭或未挂表调用都应能唤醒 owner，不应依赖固定周期轮询。

现成适配器： [Zephyr](../adapters/zephyr/)（UART DMA / IRQ、USB Bulk）、[Asio UDP](../adapters/asio_udp/)、[Astrial 串口](../adapters/astrial/)、[Astrial USB](../adapters/astrial_usb/)。
