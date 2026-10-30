# Native packet UDP 适配器

Asio `PacketUdpAdapter` 用 fixed-peer datagram 传递单个 native/NONE packet。port 由 C 持有，单 owner 执行 socket I/O 和 core，没有 adapter worker。接收容量严格大于合法 frame 以识别截断；latched wait 合并 RX/completion/stop 与有限 deadline，stop 关闭 port/socket 接纳，调用方 join owner 和外部 notify producer 后才释放存储。不自动 ARQ 或 peer 协商。

启用 WIRELINK_BUILD_ASIO_UDP_ADAPTER 并提供匹配的 standalone Asio。当前双进程 C 示例为 examples/05_zephyr_udp，生成状态由 C 持有，薄 C++ helper 接 I/O。tests/core 与安装后 packet_adapters 验证 fixed-peer、边界错误、背压、取消和包消费。本地 UDP 不是物理网络资格证明。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
