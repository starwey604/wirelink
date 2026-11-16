# 接入物理链路：先选 embedded，异步驱动用 external

[文档总览](README.md) · [节点与生成 API](../wlc/README.md) · [packet_port.h](../include/wirelink/packet_port.h) · [packet_stream.h](../include/wirelink/packet_stream.h)

Wirelink 核心不读取时钟，也不运行线程、ISR、DMA 或 socket。普通同步发送先使用默认的 `io = embedded`：节点自带端口、精确大小的收发缓冲及所选流连接器。需要异步 DMA、零拷贝、驱动 stop/drain 协议或已有 Host/Zephyr 执行器时，选择 `io = external`，由应用提供 `wl_packet_port_t`。

## 一、embedded：只实现物理收发

以下以 [遥测回环](../examples/00_telemetry/) 的 `pub` / `sub` 为例。生成同一进程使用的两个角色：

```sh
wlc compile examples/00_telemetry/telemetry.wl --all-nodes --out-dir build/telemetry
```

### 初始化与 native 接收

```c
#include "telemetry.h"
static pub_node_t publisher;
static sub_node_t subscriber;

wl_err_t pub_tx_write(pub_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return sub_rx_packet(&subscriber, data, length);
}
```

初始化 `pub_init(&publisher)` / `sub_init(&subscriber)` 并检查返回值后，调用 `pub_send_telemetry()`；owner 步进 `pub_step()` 和 `sub_step()` 即可收发。应用实现 `sub_on_telemetry()` 接收业务值，不用注册 handler 表。上面物理钩子用于短回环；实际驱动把完整包交给 `<node>_rx_packet()`，其成功返回表示已复制接收，不表示业务回调已执行。

默认初始化通过进程级 `wl_packet_instance_next()` 分配非零、不会回绕的实例号，分配失败会传递给调用者。它只保证本进程内自动分配之间唯一。链路可能残留重启前旧包时，用 `pub_init_with_instance(&publisher, id)` 接入应用的持久会话身份策略，并避免显式 ID 与自动 ID 冲突。

### TX 是整帧事务，忙后必须通知恢复

`<node>_tx_write(self, bytes, length)` 在 owner 步进期间调用：

- 返回 `WL_OK`：已在返回前同步消费并完成整帧物理发送，之后不再保留此指针。
- 返回 `WL_ERR_WOULD_BLOCK` 或 `WL_ERR_BUSY`：没有接受任何字节。待硬件或对端 RX 槽恢复可用后，调用 `<node>_tx_ready(self)`，让 owner 重试。
- 其他错误：发送失败，按节点错误/调用完成路径报告。

不能先写半帧再返回忙，也不能启动 DMA 后立即返回 `WL_OK`。如果物理设备只支持部分写入或异步持有指针，使用 external 管理真实物理租约。

回环中的可写条件是对端 RX 有空槽：owner 消费完接收包后通知发送端 `tx_ready()`。单次普通消息提交成功不等于槽位已可复用；后续提交背压时保留业务值，待步进/恢复事件后再试。

### 流接收必须保留 consumed 之外的尾部

在节点中选择 `framing = cobs_crc16` 或 `framing = length16` 后，生成入口改为：

```c
size_t consumed = 0;
wl_err_t error = sensor_rx_feed(&sensor, bytes, length, &consumed);
```

这里假定节点名为 `sensor`。返回值 `error` 是 `wl_err_t` 状态，`consumed` 输出已消费字节数。无论返回成功、背压还是坏帧，都按 `consumed` 推进输入；只重试 `bytes + consumed` 之后的尾部。RX 暂满时等待 owner 释放槽位再继续，不能丢尾部、重复喂入前缀或原地忙循环。`rx_feed` 的生产者必须串行；若 ISR 无法保留尾部，先通过 SPSC 字节 FIFO 交给 owner，再由 owner 喂入。字节确实丢失时应走所用连接器的断流/重同步路径，不能当作成功接收。

### 关闭顺序

1. 停止并确认 RX 生产者不再调用 `rx_packet()` / `rx_feed()`。
2. owner 调用 `<node>_close()`。embedded stream 节点会 abort 尚未完成的半帧 RX claim。
3. 继续 `<node>_step(now)`，直到 `<node>_quiescent()` 为真，再释放或重新初始化节点存储。

## 二、external：由驱动持有端口与物理租约

在 schema 中选择 `io = external`。静态节点初始化是 `n_init(&node, &port)`；只有显式选择 `handlers = dynamic` 时才追加 handler 表与 `user`。下段来自 [裸机回环](../examples/bare_metal_loopback.c)，同时演示 external 与 dynamic：

<!-- [docs:loopback_setup] -->
```c
wl_packet_config_t config = {.instance = 1001U + side,
    .rx_storage = &receive_storage[side][0][0], .rx_unit_size = 256U,
    .rx_storage_size = sizeof(receive_storage[side]),
    .submit = wl_packet_loopback_submit, .notify = wl_packet_loopback_notify,
    .user = &transport.directions[side]};
require(wl_packet_init(&ports[side], &config) == WL_OK);
demo_handlers_t handlers = {.on_execute = execute, .on_event = received};
require(demo_init(&endpoints[side], &ports[side], &handlers, NULL) == WL_OK);
```
<!-- [/docs:loopback_setup] -->

`instance` 必须非零；每次新建物理会话应使用应用选定的不冲突身份。`rx_storage_size` 等于 `WL_PACKET_RX_SLOTS * rx_unit_size`。端口的最大规范接收帧按 `<N>_RX_FRAME_MAX` 计算；USB/UDP 驱动若以容量检测截断，应额外预留一个字节。外设缓冲还须服从适配器的具体边界规则，而非盲目只取发送方向的宏。

### 异步 TX：submit 与 completion

owner 调用 `wl_packet_sink_fn`。驱动返回值决定是否持有传入的数据指针和 token：

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

UART 等流式链路使用 `wl_packet_stream_t`，再按需增加 `wl_byte_fifo_t` 隔离 ISR 与 owner。参考 [packet_tool_owner.c](../examples/common/packet_tool_owner.c) 与 [Zephyr UART 适配器](../adapters/zephyr/)：

- `wl_packet_stream_config_t.maximum_frame_size` 使用节点 TX 规范帧上界；`maximum_rx_frame_size` 使用 RX 上界。
- stream TX 存储至少容纳 `<N>_TX_WIRE_MAX`；RX 单元按所选连接器的实际存储要求分配，不能把 TX/RX 两个角色混为一个大小。
- ISR 使用 `wl_byte_fifo_reserve()` / `commit()`；FIFO 满导致字节丢失时调用 `wl_byte_fifo_note_gap()`。
- owner 用 `wl_byte_fifo_to_cobs()` 排入 stream；直接调用 stream `feed()` 时也必须处理 `consumed` 尾部。

### 单 owner 与安全关闭

所有节点调用、发送、步进、取消、关闭及业务回调都在同一 owner 中顺序运行。ISR / I/O 线程只能使用传输层允许的单生产者/原子入口：RX claim/commit/abort/push、FIFO reserve/commit/note_gap、TX publish 与 activity 通知；不能运行内核。

external 关闭按以下顺序完成：

1. 停止 RX 生产者并等待其退出；若外部 stream 持有半帧 claim，此时调用 `wl_packet_stream_abort_rx()`。
2. owner 调用节点 `close()`，停止新业务提交并安排 `WL_KERNEL_CLOSED` 完成回调。
3. 停止/排空物理驱动，让所有异步 TX 发布最终状态；继续 owner `step(now)`，直至节点 quiescent 且驱动确认不再访问存储。
4. 才能释放端口、stream、FIFO、节点及驱动缓冲，或用新实例号重新初始化。

## 三、Host / RTOS 事件循环

对于已有异步适配器，使用 `service + step` 推进，再以 ready/deadline 和外层锁存通知决定等待，避免唤醒丢失。以下来自可编译的 [Host UDP 示例](../examples/05_zephyr_udp/)：

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
    telemetry_t sample = {.sample = ++sequence};
    const wl_err_t error = udp_server_send_telemetry(&server, &sample);
    if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) { status = 1; break; }
    due = now + 20;
  }
  now = example_now_ms();
  uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
  if (example_udp_wait(udp, &server.core, wait) != WL_OK) { status = 1; break; }
}
```
<!-- [/docs:udp_server_loop] -->

简化 RPC 从下一次 owner step 才开始超时；`_ex` 和 SDK 使用显式时刻，排队时间计入预算。恢复可写、RX 到达、关闭或未挂表调用都应能唤醒 owner，不应依赖固定周期轮询。

现成适配器： [Zephyr](../adapters/zephyr/)（UART DMA / IRQ、USB Bulk）、[Asio UDP](../adapters/asio_udp/)、[Astrial 串口](../adapters/astrial/)、[Astrial USB](../adapters/astrial_usb/)。
