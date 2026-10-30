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
| Zephyr USB bulk | native packet | 启用所属 USBD context | `wl_zephyr_usb_bulk_service()` | 复用 link 前禁用 USBD |
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


## USB Bulk 的统一配置与接收准备

USB 专用入口是 `<wirelink/usb_bulk_defaults.h>`，CMake 链接
`Wirelink::usb_bulk_defaults`（Astrial USB target 和 Zephyr module 已传递该依赖）。
USB Bulk 只使用 `native_packet`；用一份 `wl_usb_bulk_config_t` 选择 integrity、请求容量及槽数，并在配置、
接收准备和 adapter 绑定时复用。通用 SDK 不引入 USB 或操作系统依赖。

```c
/* 应用自己的容量预算示例，不是库默认值。必须覆盖本 profile 最大 frame。 */
enum { APP_USB_RX_CAPACITY = 2304, APP_USB_RX_SLOTS = 3 };
static uint8_t usb_rx_storage[APP_USB_RX_SLOTS][APP_USB_RX_CAPACITY];
const wl_usb_bulk_config_t usb =
    wl_usb_bulk_native_packet(APP_USB_RX_CAPACITY, APP_USB_RX_SLOTS);

int err = task_endpoint_config_defaults(
    &config, environment, wl_usb_bulk_link_defaults());
if (err != WL_OK) return err;
err = wl_usb_bulk_config_apply(&config.link, usb);
if (err != WL_OK) return err;
err = task_endpoint_init_config(&endpoint, &config);
if (err != WL_OK) return err;
wl_ctx_t *link = wl_endpoint_link(task_endpoint_handle(&endpoint));
err = wl_usb_bulk_rx_init(link, usb, &usb_rx_storage[0][0], sizeof(usb_rx_storage));
if (err != WL_OK) return err;
/* Zephyr: adapter_config.link = link; adapter_config.transport = usb;
 * Astrial: adapter_config.transport = usb; UsbBulkAdapter::open(*link, adapter_config); */
```

`wl_usb_bulk_native_packet()` 是唯一工厂，使用 `native_packet` 和 NONE，不提供统一容量／槽数默认。
生成 endpoint 的 profile 必须允许 native 布局，两端显式选择相同 envelope/integrity；
旧 USB COBS 固件与当前版本线上不兼容，两端须同步升级；USB Bulk 已移除 COBS 选项、字节 ring 和 stream staging，不协商或回退。
如需 CRC16/CRC32C，在应用配置之前覆盖这份 USB 值的 `integrity`，之后复用同一值。
旧 `wl_usb_bulk_link_defaults()` 仍是仅选 NONE 的通用 defaults 值，CDC/UART 用法保持不变。

`wl_usb_bulk_config_apply()` 只修改尚未初始化的配置描述中的 envelope/integrity，
保留 payload、session、MTU、重试等字段。native 请求容量必须严格大于
`wl_frame_raw_size(max_payload_len, integrity)`，包括最大 header 和完整性字节；
等于上界、槽数不在2..8、乘法溢出等均被拒绝，拒绝时配置不变。
最终能力和生成布局检查仍由 link/endpoint 初始化执行；固定 `cobs-crc16` 不会降级 NONE。

`wl_usb_bulk_rx_init()` 是明确的初始化步骤，调用方提供并持有 native storage。
同一 link 上重复准备被拒绝，避免清掉已有 claim、队列或事件 lease。storage 必须一直
有效到 adapter quiesce、所有事件归还并关闭 link；Astrial 不再私自分配／替换队列。
两个 adapter 绑定时只读核对实际 envelope、integrity、unit 容量和槽数；不匹配或未准备
就报错，不改写已初始化的 link。Astrial 默认构造的 `transport` 不提供隐式内存预算，须由应用显式赋值。原 adapter 的 `maximum_read_size`/`maximum_rx_size`、
`unit_queue_slots` 和 Zephyr storage 配置字段由 `transport` 与显式准备步骤替代。

## 可见的 RX 故障与恢复

native frame 必须由 short packet 或显式 ZLP 结束。成功空 completion 只归还 claim；
失败、填满请求或队列提交异常会中止 claim、锁住 RX，并保留明确原因：
`BOUNDARY_LOST`、`TRANSFER`、`INVALID_COMPLETION`、`QUEUE` 或 `DISCONNECTED`。
此时不能把后缀当下一帧。普通队列背压仍在释放事件后恢复，二者不同。

故障会通知／唤醒 owner，使 readiness 至少可运行一次；`service()` 通过 `WL_ERR_IO`
报告，endpoint step/pump 因而能向应用传递失败。处理通知后 readiness 不会因永久故障
持续忙转；主动调用 service 仍报告尚未恢复的故障。应用用 Astrial `rx_fault()`／
`UsbBulkAdapterStats::rx_fault`，或 Zephyr `wl_zephyr_usb_bulk_rx_fault()`／stats 查询。
仅看 `rx_paused` 或等待 RPC 超时不足以识别这种故障。

Zephyr 的 owner 线程可调用 `wl_zephyr_usb_bulk_recover(&adapter)`，它对整个 USBD
context 执行 disable/enable，要求 host 重新枚举并配置；该 context 上其它 USB class
也会短暂断连。不能在 ISR／USB callback 中调用。成功表示请求已接受，直到实际 class
配置完成前 `rx_fault` 仍保留、stats.recovering 为 true；重复请求返回
`WL_ERR_WOULD_BLOCK`。失败返回 `WL_ERR_IO`，故障保留；enable 失败后可显式重试，
不会重复已成功的 disable。正常配置完成后清除当前故障，接收恢复。

即使重新配置发生得很快，尚未处理的故障通知仍保留；此时 service 仍报告一次 IO，
当前 `rx_fault` 可已为 NONE，而 `last_rx_fault` 保留原因。`reset_stats()` 不清除
这些状态或待处理通知。示例 `samples/zephyr/usb_bulk` 展示了报告与重配置调用。
恢复不会偷偷丢弃已完成／借出的帧，也不会重建 core/RPC session；应用若需取消事务或
更换会话，须按 endpoint 的 close/init 合同显式执行。

Astrial 应用先记录 `rx_fault()`，从 owner 线程 `quiesce()`，然后等待或发起真正的
USB reset/reconfigure／物理重连，再关闭并按相同配置重建 endpoint、准备 RX、重新 open。
对端可通过上述 Zephyr 恢复入口重建边界。单独重开 host adapter、清计数或调用
`wl_feed_recover_reset()` 不会清掉设备未读后缀，不能冒充 USB 恢复。USB Bulk 只接受 native；UART/CDC 的字节流接入按各自合同使用 COBS。
