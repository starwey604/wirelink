# Packet adapter 生命周期

[English](adapters.md) · [内核所有权](static-endpoint-kernel-api.md).

初始化 C-owned port/两个 RX unit 与新 instance，绑定后启动 adapter。串行 producer 只发布 I/O/completion，codec/业务在单 owner 执行；不再有旧 ctx、event pump 或生成的隐式 adapter service。合并 readiness、core/应用 deadline 和 latched activity，等待前扣除 owner 工作耗时。

| Adapter | Connector | 完成与关闭 |
| --- | --- | --- |
| packet loopback | native/NONE | 单 owner 实际复制到 peer 后 consume/drain，stop 真实放弃滞留传输 |
| Zephyr UART DMA | COBS/CRC16 + C-owned FIFO | 真实 TX done/RX buffer 归还，stop/service/quiesce 同步 callback |
| Zephyr UART IRQ | COBS/CRC16 + FIFO | 必须有真实 irq_tx_complete；当前 CDC 缺该能力时明确拒绝 |
| Zephyr UART poll | COBS/CRC16 + FIFO | 必须有物理 transmitter-idle query，压力不等于丢字节 |
| Zephyr USB bulk | native/NONE | USBD callback、token/MPS/ZLP，异步 stop 确认后 deinit |
| Astrial PacketUsbAdapter | native/NONE | borrowed read/write callback，stop 关闭并 join backend 后释放存储 |
| Astrial PacketSerialAdapter | COBS/CRC16 + FIFO | write callback 仅 consume，非阻塞物理 drain query 完成 TX；stop join worker |
| Asio PacketUdpAdapter | native/NONE datagram | owner-only socket I/O，latched wait interrupt；调用方 join owner/notify producer 后销毁 |
| Zephyr UDP | fixed-peer native/NONE | owner-only socket，stop/wait notification、task join、socket close |

SENT 表示在该 adapter 边界真实消费并完成；STARTED 保留源，可 inline completion，用 token 发布 CONSUMED/DRAINED 或 TX_ERROR。BUSY/FAILED 未接纳，不能随后 completion；BUSY 必须在资源就绪时通知。RPC 逻辑终态不能提前复用驱动借用字节。

关闭 core、停止真实 producer，owner 继续处理直到物理取消/完成与 kernel/port quiescence。port/staging/wake context 一直存活到 callback/worker 实际退出。stopped 标志不能代替驱动归还；重初始化需新 instance，token/operation 不回绕。

USB RX 容量严格大于合法 frame。成功空读不 dispatch；full/error/truncated read abort 并锁存边界故障，需真实 USB reset/reconfigure，不能仅 reopen。旧 completion 不得影响新 claim，保留 exact-MPS/ZLP。UART 真正失字节时 abort/resync 当前窗口，普通背压则暂停/恢复而不丢数据。函数细节见安装头，[硬件验证范围](adapter-hil.md)单独记录。
