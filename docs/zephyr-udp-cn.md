# Zephyr UDP：默认端点接入与验证

[English](zephyr-udp.md)。M1 已实现公开的 `<wirelink/zephyr/udp.h>` 接口：
原生 IPv4 socket、单一 owner、静态 RX 存储，不创建适配器线程或使用堆。
生成 ABI 与线上帧格式不变。M2 同时修复了通用 managed RPC 的取消发送回收问题，
详见[后续实施记录](zephyr-udp-progress-cn.md)。

**目前通过的是功能验收，不是实时性验收。** 当前 Zephyr 在发送 packet 池耗尽时，
即使设置 `MSG_DONTWAIT` 仍可能等待约 1 秒。详情及下一步见文末；不能把它直接放进
硬实时控制任务后，仅凭 Wirelink 的重试间隔推断最坏延迟。

想先看完整业务代码，请从[独立 client/server 示例](../samples/zephyr/udp_peer/README-cn.md)
开始；本页说明适配器合同。M2/M3 当前结果见[实施记录](zephyr-udp-progress-cn.md)。

## 最小接入方式

启用 `CONFIG_WIRELINK_ZEPHYR_UDP=y`，以及 `CONFIG_NETWORKING`、`CONFIG_NET_IPV4`、
`CONFIG_NET_UDP`、`CONFIG_NET_SOCKETS`。适配器自动选择 `CONFIG_ZVFS_EVENTFD`。
目前不支持 socket offload。网络接口、地址和路由由平台配置；本层不接管 Ethernet、
DHCP、DNS 或时钟。使用 `wl_platform_environment()` 还需启用 `CONFIG_WIRELINK_PLATFORM`
并具备可用会话随机源，不能把测试用的确定性来源带入产品。

以下是接入片段，类型由[计算器示例](../examples/01_rpc/)生成；`CHECK` 表示应用的错误
处理，`request/response` 是已有的业务 value。完整可运行验收程序见
[`udp_validation`](../samples/zephyr/udp_validation/)。

```c
static calculator_endpoint_t client;
WL_ZEPHYR_UDP_DEFINE(udp, CALCULATOR_ENDPOINT_MAX_PAYLOAD, 2);

wl_zephyr_udp_config_t network = {
  .peer_address = "192.168.1.20", .peer_port = 49101, .local_port = 49100,
};
CHECK(calculator_endpoint_init(&client, wl_platform_environment()) == WL_OK);
CHECK(wl_zephyr_udp_open(&udp, calculator_endpoint_handle(&client), &network) == WL_OK);
wl_rpc_completion_t result = calculator_endpoint_add_sync(&client, &request, &response, 1500);
CHECK(calculator_endpoint_close(&client) == WL_OK);
CHECK(wl_zephyr_udp_close(&udp) == WL_OK);
```

`open` 自动安装收发入口、service、deadline、quiesce 和 RPC waiter，不替换生成的业务
分发或 policy。已有 adapter/waiter 会被拒绝。要求端点刚初始化，尚未 step、发送或接入
其他 ingress。业务不需要手写 sink、pump hook，也不需要额外调用 UDP service。

普通 owner 循环只需先调用生成的 `endpoint_step()`，再调用
`wl_zephyr_udp_wait(&udp, maximum_ms)`。wait 返回 `WL_OK` 或 `WL_ERR_NO_DATA` 都应继续
下一轮；其他错误需要处理。wait 自动合并端点工作和截止时间，不收包、不执行业务回调。
它只能在 owner 的两轮推进之间调用，不能在端点/业务回调内部调用。

## 静态内存与网络边界

声明宏分配一个适配器状态和
`receive_slots × (payload_bound + WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC + 1)` 字节的
RX 存储。槽数为 2–8，payload bound 直接使用生成常量；无需自己设计 endpoint 结构。
所有 `private_*` 字段和 `detail/` 头文件只用于布局，业务不得修改。

仅支持 `native_packet`：一个 UDP 数据报包含一个完整 Wirelink 帧。没有 COBS 回退或
自动协商。收包直接写入 core 的 claim，校验长度和来源后才 commit；空包、错误来源、
超长/截断包直接丢弃。多出的一个字节用于检测截断，不是线上字段。
这只减少适配器暂存复制，不代表网络栈/DMA/业务 value 全程零拷贝。

对端固定为数值 IPv4 地址和非零端口，字符串只在 open 时读取。本地地址默认
`0.0.0.0`；本地端口 0 由系统选择，可通过 `wl_zephyr_udp_local_port()` 查询。
来源过滤和 CRC 都不是认证。不支持多客户端服务器、IPv6、发现或直接替换活跃对端。

`maximum_datagram_size` 是**整个 Wirelink 帧的上限**，默认 1472，对应普通 1500 字节
IPv4 MTU 减去 IP/UDP 头。模式/schema 的最大帧超过上限时 open 失败；调大它不等于
增加 schema 容量或探测了路径 MTU。适配器还设置 `IP_DONTFRAG`，路径更小时发送失败，
不会静默进行 IPv4 分片。大消息应按实际路径设置 schema/bulk 分块上限。

## 处理预算、背压与时钟

- `service_budget=0` 默认每轮最多处理 8 包，非法包同样消耗预算；达到预算立即请求
  下一轮，不等待凑批。RX 队列满时让端点先消费；若本轮随后释放了槽，wait 会通过一次
  空 claim/abort 检查恢复可读监听，避免错误地把后续数据一直屏蔽。
- socket 接受完整数据报才返回 `WL_SINK_SENT`，不是 DMA 完成或对端收到了。
  适配器不保留 TX 指针，不增加一层发送队列。
- 暂时性发送错误返回 `WL_SINK_BUSY`，由 core 保留原有 DATA/ACK。
  `tx_retry_ms=0` 默认在背压期间使用 1 ms 重试间隔，截止时间前不再次调用 socket。
  不持续等待 UDP POLLOUT，也不在平时增加每毫秒轮询。
- 重试到期后由 service 清理；即使调用已经取消、不再触发 sink，也不会留下永久的
  零延时 hint。DATA、ACK、计数器回绕和多端点隔离均有测试。
- 沿用端点时钟和 step 快照。成功发送不增加时钟读取；step 外首次 BUSY 需要取一次
  时间。同步 RPC waiter 使用生成层已算好的等待上限；模拟时钟推进时需通知等待者。

这里的预算是**包数预算，不是单次网络调用的执行时间上界**。重试间隔也不能给一个已经
阻塞在网络栈内部的调用设置超时；该限制不能靠再套一层 Wirelink 定时器消除。

## 通知、退出和统计

`notify`、`request_stop` 可由其他线程调用，但不能从 ISR 调用，也不代替业务队列同步。
Zephyr eventfd 内部使用 mutex；每次成功通知写一次，owner 一次读取合并此前累计通知，
不循环排空。不是无锁实现，也不是“整批生产者只写一次”。

关闭顺序：需要时 request_stop → owner 停止推进 → 调用**生成端点的 close** → 停止并
join 所有通知生产者 → `wl_zephyr_udp_close`。不得并发 close。端点 quiesce 停止 I/O、
忘记旧端点指针，但保留描述符直到 adapter close，以免仍在退出的生产者访问已复用 FD。

直接关闭仍附着的 adapter 返回 `WL_ERR_BUSY`，不会绕过生成层的 RPC 取消/回收流程。
重复 close 安全；旧 adapter 后续 close 不会关闭重建后的端点。对象复用前仍必须等所有
旧使用者退出。stop 能唤醒 poll，不能打断已经进入的阻塞网络栈/驱动调用。

owner 可以查询 `get_stats()`：通用收发计数、拒绝包数、真正 socket BUSY 次数与被重试门控
拦住的次数、预算命中、等待次数等。通知计数为可回绕的 32 位，其余为 owner 本地 64 位。
确定性 I/O 错误锁存在 `last_error`，重开清除；垃圾包和普通背压不锁死连接。
这些不是网卡实际发送或 CPU 测量。socket/eventfd/net_pkt/net_buf 仍消耗 Zephyr 的资源池，
需连同同栈其他用户一起配置容量。

`rx_idle_passes` 记录首次收包就没有数据的轮次。开启默认关闭的
`CONFIG_WIRELINK_ZEPHYR_UDP_TIMING` 后，stats 增加实际 send、receive、RX service
的次数、累计/最大 elapsed cycles；关闭时没有计时字段或计数器读取。
插桩包括抢占/阻塞，不是纯 CPU 时间，也不包含后续协议/业务 dispatch；计数器周期、
频率与嵌套统计限制见[插桩说明](zephyr-udp-progress-cn.md#可选插桩)。

## 验收记录与复现

2026-09-11：Zephyr `e4e6910cc19b7f11eada127e54c0b5248f413799`（4.4.99）、
SDK 1.0.1，生成器为本地 WLC `85b1bdc`，0.7.0-dev / ABI 32。未改动 WLC 工作区。

| 测试组 | 平台 | 执行结果 |
| --- | --- | --- |
| M0 socket + M1 adapter + 既有 protocol/waiter | native_sim 32/64、Cortex-M3 QEMU | 10 配置，112/112 用例通过 |
| 生成端点 UDP 验收 | 同上 | 3 配置，12/12 用例通过 |

M1 新增 12 个 adapter 用例及 4 个生成端点用例。C++17 头文件消费、自动 attach、真实
DATA/ACK 背压、静态槽复用、混合 RPC/遥测、同步等待、拒绝、取消、无对端超时和生成层
close 均通过。ACK 背压测试注入已接收的 core 单元后耗尽 packet 池；其余收发走真实
Zephyr UDP loopback。这里没有 Linux socket offload、实际 Ethernet/DMA 或 H5 CPU 结果。

在初始化的 Zephyr workspace 中，替换路径后运行：

```sh
west twister \
  -T /path/to/wirelink/tests/zephyr/integration/udp_socket_contract \
  -T /path/to/wirelink/tests/zephyr/integration/udp_adapter \
  -T /path/to/wirelink/tests/zephyr/integration/waiter \
  -T /path/to/wirelink/tests/zephyr/integration/protocol \
  -p native_sim -p native_sim/native/64 -p qemu_cortex_m3 \
  -j 2 --inline-logs --outdir /path/to/wirelink/build/udp-m1-final

west twister -T /path/to/wirelink/samples/zephyr/udp_validation \
  -p native_sim -p native_sim/native/64 -p qemu_cortex_m3 \
  -x=WIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -x=WIRELINK_WLC_EXECUTABLE=/path/to/matching/wlc \
  -j 2 --inline-logs --outdir /path/to/wirelink/build/udp-m1-generated-final
```

结果保存在对应目录的 `twister.json`、`twister.log` 和各配置的 `handler.log`。
CI 已将生成端点验收接入“先构建匹配 WLC，再运行生成示例”的步骤，不要求用户仓库内
存在开发用 `wlc/` worktree。本轮未修改产品依赖、未发布、未烧录，也未跑性能 benchmark。

## 必须保留的下一步：Zephyr 原生发送等待

此项按用户决定暂缓，保留为后续优化，不阻止实验状态的 M3 合入。
容量规划可以减少耗尽，但不能替代耗尽时的非阻塞兜底。

耗尽 TX packet 池后，三种测试平台均打印了 `UDP_TX_POOL_WAIT elapsed_ms=1010`。
这是模拟系统时间下的阻塞复现，不是 1010 ms CPU 消耗，也不是板端延迟测量。
仅记录诊断，不把“必须至少等一秒”写成测试断言，以免固化上游问题。

已核对本地 `subsys/net/ip/net_context.c`：`context_alloc_pkt` 使用固定的
`PKT_WAIT_TIME = K_SECONDS(1)`。原生 `sendto`、`send`、`sendmsg` 共用此路径，
`MSG_DONTWAIT` 和 `SO_SNDTIMEO` 都没有替换这个固定分配超时。
manifest 中的 [Zephyr v4.4.0](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/subsys/net/ip/net_context.c#L2701)
也存在同样调用，但本轮运行结果仅代表上述实际构建版本。

后续应在隔离的 Zephyr 改动中使分配路径尊重非阻塞/时间上限，再验证 packet **和** buffer
耗尽；之后才进行 H5 驱动锁、DMA 描述符压力、网络线程优先级及控制线程受扰测试。
发送前读取全局池空闲数量存在竞争，不能当作保证；直接改用 net_context 也绕不开共用路径。
本轮没有修改仓库外 Zephyr，不能把 M1 的功能通过描述为实时性问题已经解决。
