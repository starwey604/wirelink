# Asio UDP：生命周期、存储与边界

[English](udp-adapter.md)。这是可选的 C++20 主机适配层，不把 Asio、socket、堆或等待
引入 C11 核心和生成的业务代码。启用 `WIRELINK_BUILD_ASIO_UDP_ADAPTER` 并指定
`WIRELINK_ASIO_INCLUDE_DIR`；构建教程时会自动启用。安装包目标为 `Wirelink::asio_udp`，
旧构建树别名 `wirelink::asio_udp` 仍保留；主机 CI 固定 standalone Asio 1.38.1。

## 默认端点接入

先初始化生成端点，再把其 `wl_endpoint_t&` 通用入口交给 `UdpAdapter::open()`。
这个重载自动 attach service、quiesce 和 deadline，不替换业务 hooks。
已有适配器时先报错，不修改连接。发送前用 `set_peer()` 选择数值地址和端口；
更换对端需要重新建立连接，不能沿用原来的 RPC／会话状态。
本层不加入 DNS、自动发现、多路复用或多客户端 RPC 服务器。

端点存储必须活到适配器销毁。关闭端点会停止适配器；先销毁适配器时，会先关闭关联端点，
然后释放它拥有的 RX 队列。端点关闭后重新初始化，再销毁旧适配器，不会关闭新的端点实例。
旧适配器仍活跃时，不得复用端点。

高级 `open(wl_ctx_t&, ...)` 只安装 sink，不自动接 pump；调用者管理调度，
并在释放适配器 RX 存储前停止使用该 core，后续重新初始化再用。
所有操作都在单一 owner 执行，包括等待和关闭，不支持其他线程并发 close。

## 分帧与有界存储

| 模式 | 一条 UDP 数据报的内容 | 接收路径 |
| --- | --- | --- |
| native packet，教程默认 | 一个完整 Wirelink 帧 | 直接接收到 core unit-queue claim |
| 显式旧 COBS stream | 一个 COBS 单元 | 暂存并检查数据报长度，再复制进 stream DMA claim |

封装和校验来自已初始化连接，不再强制 `NONE`。教程沿用默认的 native packet／CRC32C。
切换传输模式需同步对端，不自动识别或回退。COBS 只是显式兼容路径。

`maximum_datagram_size=0` 按连接推导完整帧上限；显式值必须覆盖该上限且不超过 65507。
native 模式打开时分配 `receive_slots × (maximum + 1)` 字节，槽数默认 4、范围 2–8，
不是每收一包就分配。多出来的一字节用于识别某些平台会静默截断的超长数据报，
空包、超长包均丢弃，绝不把截断前缀作为有效包提交。
旧 COBS 模式暂存一包，等待 ring 有空间后再发布；这比旧 stream 接收多一次暂存复制，
native 路径没有这次复制。并不承诺 Asio 或其他主机操作完全无动态分配。

每次 `service()` 最多处理 `service_budget` 包，默认 8；native 队列满时先让 owner
消费释放。UDP／系统队列仍可能丢包。统计记录接收、拒绝、背压与 I/O 错误。
未知来源只丢弃并计数，不把其他调用一起置为失败。

## 等待与截止时间

先调用生成的 `endpoint_step()`，再用 `wl_endpoint_get_hint()` 合并链路、RPC 与业务截止时间。
`wait_for_activity()` 等待 socket 可读，发送背压时也等待可写，最多等待指定时长。
它不运行 Wirelink／业务回调，也不创建后台线程；就绪只表示下一轮可能推进。
返回前会取消、清理尚未完成的就绪回调。可观察 `wait_calls`、`wait_timeouts` 和
`activity_notifications`。

默认端点接入不再额外添加每 1 ms 的轮询截止时间；旧 bare-link 路径保留 `poll_interval`
供已有调度器使用。不能据此直接宣称固定比例的延迟／CPU 改善；应测实际负载和系统调度。
本教程是正确性示例，不是固件或网络性能测量。

## 使用范围与验证

教程只绑定 `127.0.0.1`，并固定对端。保留的“首包学习对端”是显式旧功能，
不是认证或经过验证的发现协议。来源端口过滤、CRC 同样不认证发送者。
本地包长上限不等于路径 MTU 探测；进入非受控网络前，应另外设计避免 IP 分片、
发送速率、拥塞和安全策略。

`tests/tutorials/udp_processes.py` 启动真正的 C 程序；Python 只转发／丢弃／重复／重排数据报，
不伪造 ACK、不实现 RPC。覆盖正常结果、业务拒绝、请求／响应重传、重复、重排与完全丢包。
适配器测试覆盖 native／COBS × NONE／CRC32C、队列满、等待超时与唤醒、来源过滤、
超长包与关闭后重建。Release 检查不依赖会被 `NDEBUG` 删除的 assert。
旧单进程示例保留在 `tests/tutorials/loopback/` 做确定性回归。

本轮保持生成 ABI 20 和核心冻结帧／codec 字节不变。
客户端重建、调用编号复用后的旧响应隔离限制仍然存在，见 [RPC 合同](rpc-runtime-cn.md)。

### 下一轮：统一发送时间的边界

核心用最近一次 `wl_poll()` 的时间标记发送；生成 RPC 的 `now_ms` 目前只设置 RPC
截止时间，不同步链路时钟。首次 step 前发送，可能在第一次传入真实时钟时提前重传。
加法客户端已在提交前调用一次 `endpoint_step()`；一般应用应在唤醒后先推进，再提交新业务。
消除这组隐式的双时间依赖是下一轮 API 项，不能说 UDP 就绪等待或现有 RPC 参数已解决它。
