# 实现并绑定传输

[English](adapters.md) · [Kernel API](kernel-api-cn.md) · [Wire 格式](packet-protocol-cn.md)

传输集成提供 packet sink、RX producer、owner 侧 service/readiness 和锁存唤醒。先选择与物理边界一致的 connector：

| 传输 | Connector | 边界 |
| --- | --- | --- |
| USB bulk、UDP、RAM loopback | native/NONE | 一个完整规范 DATA 包 |
| UART / serial | COBS/CRC16 | 带 CRC 和零分隔符的字节流 |
| 长度分隔流 | length16/NONE | 两字节长度加规范 DATA |

使用 [profile-v2 binding](link-profiles-cn.md)生成对应 route 和存储。分帧、驱动所有权和 RPC completion 是不同合同。

## 启动 I/O 前绑定存储

1. 分配稳定的端点、C 持有的 packet port、两个 RX unit、adapter 和唤醒上下文。流式传输还需 connector TX scratch 与所需 SPSC byte FIFO，DMA 另外需要实际驱动缓冲区。
2. 在任何发布可能调用唤醒前初始化外层 wake。使用新的非零通信 instance，检查所有初始化返回值。
3. native packet 将 `wl_packet_config_t.submit/notify/user` 绑定到 adapter 的 submit/notify 函数和 adapter 上下文。先初始化 port，再按下文特定顺序绑定/启动 adapter。
4. 流式 port 的 `submit` 为所选 `wl_packet_stream_*_submit`，`notify` 为 `wl_packet_stream_notify`，`user` 为 stream 对象。stream config 的 `physical_submit`、`notify`、`user` 指向实际 adapter 及其 wake。初始化 FIFO 暂存时提供能唤醒 owner 的 notifier。生成的 `*_kernel_transport_init` 组装 port/选定 connector，不组装驱动或 FIFO。
5. 用 port 和 handler 初始化生成端点，全部引用对象就绪后才启动输入。初始化本身不得分发业务工作，也不得与活动 I/O 竞争。

完整 C-owned 组装见 [`packet_tool_owner.c`](../examples/common/packet_tool_owner.c)，更小的 native 路径见 [loopback 示例](../examples/bare_metal_loopback.c)。C++ adapter 接收 C 原子存储的 opaque pointer/reference，不能用 `std::atomic` 替换，也不能复制活动 port/FIFO 对象。

## RX 发布完整包，不执行回调业务

native packet 由一个串行 producer claim 可写 RX 存储，接收/复制进去，再 commit 实际非零长度。失败/空接收应 abort，不 commit。改变所有权前验证保存的 claim/token、容量和当前驱动请求；过期 completion 不能 abort 或 commit 新 claim。只有 owner borrow、route 和 release RX，驱动回调不运行 codec 或业务 handler。

字节流 producer 将字节暂存到 C-owned FIFO，由 owner service feed 所选 connector。直接 connector feed 也只有一个 producer。压力下保留所有未消费后缀，存储可用后恢复；存储满不等于字节丢失。实际 overrun/不连续必须报告 gap，abort/重新同步 partial parse。COBS 可以丢弃到下一个 delimiter；length16 在任意丢字节后需要外部恢复边界。见 [stream 接口](../include/wirelink/packet_stream.h)和 [FIFO 接口](../include/wirelink/byte_fifo.h)。

## TX 报告源生命周期和物理边界

sink 在 owner 上同步运行。SENT 表示在**该 adapter 边界**源已消费且物理传输完成；STARTED 接纳租约，承诺带 token 的 completion，可以在返回前内联发布；BUSY/FAILED 不接纳，也不能稍后完成该次尝试。BUSY 必须在资源恢复后发布活动。UDP send 的边界是本地 socket 接纳，不是对端送达。

源不再被读取时发布 CONSUMED，已接纳传输完成时发布 DRAINED，失败/取消结束所有权时发布 TX_ERROR。DRAINED 隐含 CONSUMED，TX_ERROR 隐含两者。只有 owner take 才结束 port 的活动 flight。即使逻辑 timeout/cancel，kernel 仍将租用 snapshot 保留到 DRAINED。返回值、flags 和 token 验证的完整定义见 [port API](kernel-api-cn.md#提交物理-tx)。

stream 的物理源是已编码 TX scratch，不是原始规范 snapshot。stream 和 scratch 必须存活到物理 completion/stop acknowledgement；发布 completion 时保留原始 port token。队列空、回调计数和 RPC 终态均不能确立物理 drain。

## Service、readiness 与等待由 owner 执行

按需在 kernel step 前后调用 adapter service，解析暂存输入、恢复暂停 RX、发布延后 completion 和重试资源。睡眠前合并 core readiness/deadline、adapter readiness/deadline 和应用工作。`wl_io_readiness_t.next_deadline_ms` 是相对毫秒提示，`UINT32_MAX` 表示无 deadline；`blocked` 是状态提示，不是 runnable 工作。

使用锁存 wait/notify，避免查询与等待之间的活动丢失。notify 必须非阻塞，并适用于其 I/O/ISR 调用者；不能从中重入 adapter/kernel owner 操作。owner 工作后重新计算有限等待的剩余时间。kernel activity 是由 step 消费的合并提示，不代替外部事件。[Host owner loop](../runtime/host/src/executor.cpp)实现了这一组织方式；[Zephyr wait](../runtime/zephyr/include/wirelink/zephyr/wait.h)提供不含内部线程的静态锁存 waiter。

## UART 和 host serial 配置

UART 路径使用 COBS/CRC16 加 FIFO 暂存，不得与另一个 UART adapter 或 console 共享 RX producer。按实际板卡配置引脚、流控和缓冲区放置；平台 cache hook/attribute 属于平台设置，不放入 core。

### Zephyr IRQ

启用 `CONFIG_WIRELINK_ZEPHYR_UART_IRQ=y`，使用 UART、port、FIFO 和 stream 组装 [`wl_kernel_uart_irq_config_t`](../adapters/zephyr/uart_irq/include/wirelink/zephyr/kernel_uart_irq.h)。init 要求驱动提供真实 `irq_tx_complete` 能力，不能只有 FIFO-empty 中断。缺失能力时返回 `WL_ERR_NOT_SUPPORTED`；当前 CDC 路径属于该拒绝场景。

FIFO 接纳消费源内存后，关闭 TX 中断，由 owner service 检查 line idle。`drain_check_ms=0` 选择 `1 ms`，非零值不超过 `INT32_MAX` 毫秒。drain pending 时 readiness 提供该 deadline。completion query 为负表示错误，不是成功。`service`、`readiness`、`wait` 使用 owner 单调毫秒时间。

`wl_kernel_uart_irq_stop` 在 binding lock 下关闭 IRQ source、分离/同步回调、abort partial stream，并为剩余 transfer 发布 TX_ERROR。这是取消，不是成功的 line-idle 结果。port/wake 存储必须存活到最终 kernel completion 处理完。

### Zephyr DMA

启用 `CONFIG_WIRELINK_ZEPHYR_UART_DMA=y`，为 [`wl_kernel_uart_dma_config_t`](../adapters/zephyr/uart_dma/include/wirelink/zephyr/kernel_uart_dma.h)提供两份互不重叠、支持 DMA 的 `rx_buffers`。它们与 FIFO、packet-port RX unit 分开。`timeout_us`、`tx_timeout_us` 是驱动微秒 timeout，不是 RPC tick；负值必须是 `SYS_FOREVER_US`。cache prepare/complete hook 必须遵守驱动 buffer 所有权。

驱动 TX_DONE 仅表示内存消费时，设置 `wait_for_tx_idle=true`，由 owner service 查询 `uart_irq_tx_complete` 确认真实 drain。只有驱动 TX_DONE 确实满足所需物理边界时才能为 false。`drain_check_ms=0` 选择 `1 ms`，`tx_retry_ms=0` 也选择有界 `1 ms` 重试，因为 async UART 拒绝提交后没有通用 writable event。两个字段均不超过 `INT32_MAX` 毫秒，必须合并 `wl_kernel_uart_dma_readiness` 提供的 deadline。

`stop` 开始异步关闭，继续 `service` 到 stats 中 `started=0`、`stopping=0`；RX-disabled 不等于 RX-buffer-return。`wl_kernel_uart_dma_quiesce` 结束驱动所有权、service 必需进展、等待已分发 callback、分离 binding 并 abort partial RX。之后还要 kernel quiescence 和外层 notifier 退出，才能释放存储。

### Zephyr polling

启用 `CONFIG_WIRELINK_ZEPHYR_UART_POLL_EXPERIMENTAL=y`。[Poll 配置](../adapters/zephyr/uart_poll/include/wirelink/zephyr/kernel_uart_poll.h)要求调用方提供 RX worker stack/priority，以及及时、非阻塞的 `tx_idle` 回调。`uart_poll_out` 消费源，不证明 line idle。回调正值表示 drained，零表示 pending，负值表示错误。`drain_check_ms=0` 选择 `1 ms`，非零值不超过 `INT32_MAX`。owner service/readiness 推进检查，TX 本身在 owner 上同步执行。

request stop 唤醒 RX worker，再由 owner `stop` join。物理 TX pending 时 stop 可返回 `WL_ERR_WOULD_BLOCK`，idle query 失败时可返回错误；adapter/stack/port/stream 存储必须保持有效，继续关闭到 stop 成功。不得从 worker、notifier 或 sink stop/join。阻塞的 `uart_poll_out` 没有通用取消 API。见 [polling 指南](../adapters/zephyr/uart_poll/README.md)。

### Astrial serial

启用 `WIRELINK_BUILD_ASTRIAL_ADAPTER=ON`，通过 `WIRELINK_ASTRIAL_SOURCE_DIR` 提供 Astrial backend。[`PacketSerialConfig`](../adapters/astrial/include/wirelink/astrial/packet_serial.hpp)要求非空 `drained`，`drain_check_ms` 为 `1..INT32_MAX`，默认 `1 ms`，这里零无效。`drained(drain_user)` 以正/零/负分别报告 physical drain/pending/error，必须非阻塞。write callback 仅发布 CONSUMED；owner `service(now_ms)`、`readiness(now_ms)` 完成 drain 检查。`stop()` 关闭/join backend worker 后，再 abort staging 并按需结束保留 TX。自动重连关闭，重启时用新身份重建已停止的集成。

## Native USB bulk 配置与恢复

两种 native USB adapter 都要求 `maximum_frame_size` **严格小于** packet RX unit/read capacity，以区分填满 read request 和合法帧在 short packet/ZLP 结束的情况。成功空读 abort，不分发。错误/满容量读 abort，并锁存 boundary/transfer fault，预期关闭取消除外。过期 buffer/token callback 不能改变当前 claim；不得把边界不确定的后缀解析为下一个包。

### Zephyr USB device

启用 `CONFIG_WIRELINK_ZEPHYR_USB_BULK=y` 和 next USB device stack。用 `wl_kernel_usb_bulk_submit`、`wl_kernel_usb_bulk_notify`、`user=adapter` 初始化 port，再在 `usbd_init`/`usbd_enable` 前调用 [`wl_kernel_usb_bulk_init`](../adapters/zephyr/usb_bulk/include/wirelink/zephyr/kernel_usb_bulk.h)。该 class 绑定一个 adapter。使用初始化后 descriptor 分配的 endpoint address，不假设固定地址。TX 在 exact-MPS 长度时请求 ZLP，实际 UDC 必须保持该 completion 边界。owner service arm RX 并报告 fault。

`wl_kernel_usb_bulk_recover` 仅限 owner/thread，在 callback 外执行，要求存在 fault 且 adapter 非 stopping。它 disable/enable 整个 USBD context，需要 host 重新 enumeration/configuration。调用成功表示开始恢复；fault 保持锁存，直到 configuration 启用 class。重复 enable 仍 configured 的 endpoint 不会清除 fault。该操作保持 kernel instance 和排队/租用工作。完整端点 teardown/restart 则需要新身份。

关闭时 `stop` disable USBD，实际 cancellation callback 归还 buffer 前可返回 `WL_ERR_WOULD_BLOCK`。要求 `stopped`，继续 step 消费最终 port completion/RX，再 `deinit`；deinit 要求物理确认和 port quiescence。外层 wake context 必须存活到 callback 也退出。板卡/驱动配置见 [USB sample](../samples/zephyr/usb_bulk/README.md)。

### Astrial USB host

启用 `WIRELINK_BUILD_ASTRIAL_USB_ADAPTER=ON`，准备 backend 和 USB 构建依赖。在 port init 前构造 [`PacketUsbAdapter`](../adapters/astrial_usb/include/wirelink/astrial/packet_usb.hpp)，绑定其静态 submit/notify 和 `user=adapter`，再 `start(PacketUsbConfig)`。RX unit 容量还必须为 endpoint IN MPS 的倍数，start 验证 IN/OUT MPS 非零。同一时间有一个 borrowed read，自动重连关闭。backend 必须正确实现 USB transfer 边界和 exact-MPS/ZLP。

fault 是 sticky。stop 会 join borrowed read/write callback；显式 reset USB framing/reconfigure device 后，以新身份重建 adapter/port。仅重新打开 host 对象不会重置远端边界。

## UDP 和 loopback 边界

Asio [`PacketUdpAdapter`](../adapters/asio_udp/include/wirelink/asio/packet_udp.hpp)和 Zephyr [`wl_kernel_udp_t`](../adapters/zephyr/udp/include/wirelink/zephyr/kernel_udp.h)在 owner 上执行 socket I/O。最大 datagram size 必须严格小于每个 RX unit，read 多使用一个字节以拒绝超限输入。`service_budget` 为 `1..16`，Asio 默认 `8`。空、超长/截断、错误 peer datagram 被丢弃，不需流式重新同步。可选 `decode` 在 canonical commit 前执行选定的原地 framing 转换，不是 wire 格式自动检测。

Asio 绑定本地数字地址，通过显式 `set_peer` 或选择启用 `learn_peer` 从有效包学习 peer；peer learning 不是认证。writable socket readiness 唤醒 BUSY TX。Zephyr 使用配置的数字 IPv4 local/peer 地址，以及 `retry_ms`，其中 `0` 选择 `1 ms`，值不超过 `INT32_MAX`；clock、readiness/wait limit 使用毫秒。网络接口单独配置，wait 不分发业务回调。

在 owner 上关闭。Asio stop 关闭 socket 并 abort 保留的 idle RX claim，销毁 adapter 前 join 等待 owner 和所有外部 notifier。Zephyr `request_stop` 打断 wait；同步外部使用者/owner 后，`close` 释放 socket/eventfd。notifier 存储必须存活到其 producer join。

`wl_packet_loopback_t` 保留源字节，直到 owner [`wl_packet_loopback_service`](../src/packet_loopback.c)将其复制到 peer RX claim，之后才发布 DRAINED。peer RX 满时 transfer 保持 pending，stop 用 TX_ERROR 放弃实际 pending transfer。两个方向共享同一 owner；它是 RAM 模型，不是并发物理链路。

## 完成关闭并验证受影响边界

在 owner 上 close kernel，停止真实 producer，完成 adapter 特定 acknowledgement/join，并继续 step 处理逻辑终态、排队 RX 和最终物理 completion。同时要求 kernel quiescence **及** adapter 静默，才能释放端点、C 原子、暂存、驱动 buffer、worker stack 或 wake context。只有达到该边界后才能使用新的非零 instance 重启，counter/token 不回绕到旧工作。

[开发指南](development-cn.md#从修改找到决定性检查)将 adapter 修改映射到 `static_uart`、`static_usb_bulk`、`static_udp` 和受控 host fixture。覆盖内联/延迟 completion、压力、过期 token、保留 buffer 时 stop，以及 fault recovery。硬件资格还需要实际 driver/board 和 [HIL 证据](adapter-hil.md)。
