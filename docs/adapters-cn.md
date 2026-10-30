# Adapter 生命周期

[通知、关闭与 TX consumed/drained](execution-contract-cn.md)

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
| Asio UDP | native packet；显式旧 COBS | `open()` 绑定 socket；端点重载自动 attach | `service()`；按截止时间 `wait_for_activity()` | `quiesce()` 或关闭端点 |
| Zephyr UDP（实验） | native packet | `wl_zephyr_udp_open()` 自动接入端点 | 生成的 step 自动 service；`wl_zephyr_udp_wait()` | 先关闭生成端点、join 通知生产者，再 `wl_zephyr_udp_close()` |

Zephyr UDP 的静态声明、背压时钟和已知原生栈等待限制见
[接入与验证](zephyr-udp-cn.md)。普通生成端点用户不应额外手动调用 adapter service。

UART DMA stop 等待实际 RX buffer 归还与 TX completion。驱动归还 buffer 后，stop
丢弃尚未发布的 staging 字节，不要求关闭中的 owner 消费 core RX ring；quiesce
还会等待已进入的 callback 退出。重新 start 接收流时，由 owner 清理或重同步其缓冲输入。

ESP32 UART IRQ 构建把 IRQ 与 owner 执行桥接代码放入 IRAM。owner 桥接依赖 Espressif
linker 对 `RAMFUNC_SECTION` 的支持；已测 Zephyr revision 见
[执行报告](execution-refactor-round-1-result-cn.md)。ESP32-S3 实测额外使用约 2.8 KiB IRAM。

`CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING` 可选地统计有效 UART callback 的进入次数
和内部耗时。`callback_irqs` 每次 callback 加一；`rx_irqs`、`tx_irqs` 仍是就绪源处理次数，
不能直接当作硬件中断数。`callback_ticks` 不包含驱动 ISR 与 callback 锁的获取／释放，
使用 `k_cycle_get_32()`，计数器按 32 位回绕。关闭选项时没有新增计时读取、存储或 ISR 工作；
开启时 adapter 和 stats 的结构布局增加对应字段，应用与库须使用一致配置。

ESP32 短帧的尾部延迟还受驱动 RX timeout 控制。本仓库提供
[本地 Zephyr 补丁](../adapters/zephyr/patches/README.md)，增加默认值为 22 字符周期的
`CONFIG_UART_ESP32_RX_TIMEOUT`。该设置作用于该驱动的所有 UART 实例；Wirelink core
不设置硬件 timeout。22／8／4／2 的独立对照见
[UART timeout 实测报告](uart-rx-timeout-optimization-cn.md)。

UDP 对端可以晚启动或先关闭。Windows 下关闭 `SIO_UDP_CONNRESET` 通知，避免之前
数据报触发的 ICMP Port Unreachable 终止后续接收。对端不可用仍由可靠事务/RPC
超时反映；UDP 发送成功不保证送达。参见
[Winsock IOCTL 文档](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls)。

RX queue 满或 RX lease 被保留时，Asio UDP 仍等待锁存通知或 deadline；没有可消费
空间时不订阅 socket RX。取消会先排空 socket 和 timer handler，再释放 waiter 的
局部状态。`WL_NO_DEADLINE` 选择无限等待。

类型化 adapter 暴露平台专用配置与统计；C core 不增加虚调用，也不拥有等待原语。

## Owner Loop 模式

把 `service` 和 `quiesce` 填入 `wl_pump_hooks_t`，分别接入 adapter 与生成
runtime 的 deadline hint，再用 `wl_pump_get_readiness()` 决定下一次等待时间。每次
wake 都先运行一次 `wl_pump_step()` 再查询新 hint，避免 sink 背压演变为零延时忙循环。

查询到等待之间有其他工作时，应保留查询时间戳。endpoint 可先读 `wl_endpoint_now`，
再调用 `wl_endpoint_get_readiness_at`；进入 waiter 前以新时间调用 `wl_wait_remaining`
扣除已经消耗的时间，并与调用者业务期限取最小值。raw/manual owner 也可使用传给
`wl_pump_get_readiness` 的快照和同一辅助函数。锁存确认、外部队列、部分 RX、stop
与有限 timeout 的完整规则见[执行与等待](execution-contract-cn.md)。

USB CDC IRQ 样例使用 endpoint 时钟快照调用 `_at`，在入口前以 `wl_wait_remaining`
扣除有限预算的查询耗时，再调用 `wl_endpoint_waiter` 返回的 waiter。runnable／过期
工作使用零时间等待，同时观察 stop，保证不阻塞。通知只在 wait 中消费，每次唤醒
都进入下一次有界 endpoint step。该 owner helper 只属于样例，不是 adapter API。

`wl_zephyr_uart_irq_wait_ready` 已删除，不保留兼容包装。raw/manual 的
`wl_zephyr_uart_irq_wait` 继续接受调用者提供的 kernel timeout；需要微秒预算的
消费者继续使用已有 Zephyr timeout helper。attach、waiter descriptor、通知和停止职责保留。

每条 link 只能选择一个 ingress：复制式 stream 用 `wl_feed_bytes()`，直接 stream
用 reserve/commit 或 DMA claim，native packet 用 unit queue。同一已初始化 context
不得混用 ingress family。


## 在初始化前选择 USB 默认配置

通用配置入口新增第三个参数 `wl_link_defaults()`（CRC16）；USB Bulk 接入使用公共
`<wirelink/usb_bulk_defaults.h>` 中的 `wl_usb_bulk_link_defaults()`（NONE）。
普通生成 SDK 只依赖通用 C 头，不引入 USB、libusb、C++ 或 Zephyr 依赖。
使用 CMake USB 集成头时链接 `Wirelink::usb_bulk_defaults`；Astrial USB target 和
Zephyr 模块会自动提供该头的 include 路径。

```c
#include <wirelink/usb_bulk_defaults.h>

int err = task_endpoint_config_defaults(
    &config, environment, wl_usb_bulk_link_defaults());
if (err != WL_OK) return err;
/* 可选的高级覆盖：config.link.integrity = WL_INTEGRITY_CRC32C; */
return task_endpoint_init_config(&endpoint, &config);
```

快捷入口同样接受第三个 defaults 参数。`create()` 接收最终 config，不重新套用默认值。
裸 core 使用 `wl_config_apply_link_defaults(&config, wl_usb_bulk_link_defaults())`，
只写 integrity；payload、session、envelope、MTU、重试等字段保留。应用默认值后再做用户覆盖，
随后初始化 link，最后绑定 adapter。adapter 绑定不会改写配置；非法枚举被拒绝，
固定 `cobs-crc16` 对 NONE 返回 `WL_ERR_NOT_SUPPORTED`，不会回退。

native USB Bulk 中一个完整 frame 由短包或显式 ZLP 结束；两端必须显式选择
`native_packet`，它与 COBS 线上不兼容。接收请求必须严格大于
`wl_frame_raw_size(max_payload_len, integrity)`（包含最大 frame header），并提供至少
两个 unit 槽，数据区生命周期覆盖 adapter。成功的空 completion 归还 claim，
不发布业务事件，也不计为 IO 错误。

失败或恰好填满的 native 请求无法证明已消费终止边界：中止 claim、保留错误并停止 RX。
恢复前必须复位／重新配置 USB 传输，再重新启用 Zephyr USBD，或以重新初始化的 link
重开 Astrial adapter；单独重开主机 adapter 不会清掉设备中未读完的后缀。
普通队列背压在释放事件后恢复，边界故障不会按背压自动恢复。关闭期间的取消归还 claim，
不发布部分数据。

两端必须选择相同 integrity。USB preset 不改变 profile 的 envelope，也不关闭长度检查、
RPC deadline、replay 或业务校验。USB CDC IRQ 示例也显式选择该可信直连 USB preset，
对应主机工具的 CDC 模式；通用 UART IRQ adapter 不推断介质，普通 UART 仍选择通用 CRC16。
USB 示例此前已使用 NONE；本次入口变化不代表新的性能收益。
