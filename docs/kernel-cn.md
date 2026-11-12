# kernel 如何工作

[English](kernel.md) · [API 参考](kernel-api-cn.md) · [运行遥测](getting-started-cn.md)

Wirelink 用带类型的普通消息和有界 RPC 连接两个端点。应用提供存储、一个所有者（owner）、时间快照和传输。C11 kernel 负责分发输入、排队已编码输出和完成调用，不分配内存、不读时钟、也不启动线程。

## 先运行两个端点

在仓库根目录构建使用已检入生成代码的示例：

```sh
cmake -S . -B /tmp/wirelink-docs-core -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/wirelink-docs-core --parallel
/tmp/wirelink-docs-core/examples/wirelink_bare_metal_loopback
ctest --test-dir /tmp/wirelink-docs-core --output-on-failure
```

程序输出：

```text
8 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent
```

[`bare_metal_loopback.c`](../examples/bare_metal_loopback.c) 用一个 owner 和 RAM 拷贝传输推进两个端点。时间单位是一次循环；`500U` 表示 500 个 tick，不是 500 毫秒。它不需要 WLC 可执行文件。要用匹配的本地 WLC 从自己的 schema 生成端点，请继续阅读[遥测](getting-started-cn.md)和 [RPC](tutorial-rpc-cn.md) 教程。

## 从 schema 到物理 I/O

1. **Schema 与代码生成**定义消息字段和 ID。profile-v2 binding 选择 route、RPC 请求/响应对和一种 connector。WLC 生成 codec、带类型的 handler、route 分发和有界端点存储。
2. **Route 与 kernel**把接纳的值编码为快照。kernel 调度调用、终态、RX 和 TX。生成的 route 解码借用输入，在 owner 上执行对应的带类型 handler。
3. **Packet port 与 connector**交换规范的 native/NONE DATA 包。流式 connector 在此边界外添加 COBS/CRC16 或 length16/NONE。
4. **Adapter**通过实际驱动移动包或字节，发布输入、资源活动和带 token 的物理完成。

生成端点包含共享的 `wl_kernel_t`，不会另建 RPC 引擎。Host Executor、C++ Session 和生成 SDK 使用同一个 kernel。独立 codec 和 FIFO/LATEST/outbox 容器也可单独使用。各层合同分别见 [profile](link-profiles-cn.md)、[wire 格式](packet-protocol-cn.md)和 [adapter 生命周期](adapters-cn.md)。

## 一个 owner 执行业务工作

owner 串行执行接纳、取消、`step`、就绪查询和关闭，也执行消息 handler、RPC handler、终态回调和 `on_error`。owner 可以是裸机循环、RTOS 任务或 host 线程；多个端点可以共享同一个 owner。

I/O producer 通过 packet port 发布数据。RX 只有一个串行 producer 和一个 owner consumer；来自不同驱动回调的 RX claim/commit/abort 必须串行化。它们不执行 codec 或业务 handler。串口输入也可以先由 I/O producer 写入 C 持有的 byte FIFO，再由 owner 解析为包。

`notify` 是业务回调执行规则之外的通知函数：它可以在 I/O 上下文运行，只应触发适当的锁存唤醒。sink 在 owner 提交 TX 时同步执行，可以在返回前发布 completion，但不能重入 kernel 执行。

### 存储有几种不同的容量

固定逻辑上限集中定义在 [API 容量参考](kernel-api-cn.md#capacities)。每个端点有四个 outbound call 槽、四份 request snapshot、四份 reply snapshot、一份普通消息 snapshot 和九索引接纳 FIFO。RX 有两个 unit；port 同时只允许一笔物理 TX。

这些容量计数的对象不同。调用在 timeout 时可以释放逻辑槽，但请求 snapshot 仍被 I/O 租用。因此，四个空闲调用槽并不一定意味着四份可用请求 snapshot。九索引 FIFO 按接纳顺序排列请求、响应和普通消息，保存的是索引，不是额外 payload。

WLC 根据所选 codec 的上界，分别确定请求、响应和普通消息存储的字节容量。生成的 `*_KERNEL_PACKET_FRAME_MAX`、`*_KERNEL_WIRE_FRAME_MAX` 表示字节容量，不是并发度。RX unit、流式 TX scratch、byte FIFO 和硬件/DMA 缓冲区各有字节容量；硬件缓冲不会提高 kernel 的调用上限。

C 定义 packet-port 和 byte-FIFO 的原子对象存储；C++ 接收 opaque pointer，不能用 `std::atomic` 替换。端点、port、RX unit、connector 存储、adapter 和唤醒上下文都必须存活到关闭完成。

## 跟随一条普通消息

仓库内 demo 发送 ID 为 `42` 的 `event`：

1. `demo_kernel_send_event` 在返回 `WL_OK` 前，将传入的 `event_value_t` 编码到普通消息 snapshot。之后调用方可以覆盖源对象，包括通过 view API 传入的源字节。
2. 后续 owner `step` 将排队的规范 DATA 包交给 port sink。接纳不是物理送达。普通 snapshot 仍被占用时，再次发送返回 `WL_ERR_WOULD_BLOCK`，不接纳消息；普通发送没有终态回调。
3. loopback adapter 保留源指针，在 service 时把数据复制进对端 RX claim，commit 该包，并向发送端 port 发布 `DRAINED`。其他传输使用各自实际完成边界。
4. 接收端 owner 借用已提交的包。生成 route 解码并执行 `on_message_event`。view 和其中引用的字节只借用到 handler 返回；后续要用的值必须复制。
5. 接收端释放 RX。发送端在 `step` 中取得 completion，并在物理 drain 后释放 snapshot。

未知消息 ID 在配置了 `on_error` 时报告 `WL_ERR_NOT_SUPPORTED`。格式错误的包先释放再报告错误，使后续包可以继续处理。

## 跟随一次成功 RPC

同一 demo 的 `execute` 使用请求 ID `40` 和响应 ID `41`：

1. `demo_kernel_execute_start` 接收拥有存储的 request value、`now`、timeout、带类型回调/上下文和输出 handle。返回 `WL_OK` 时，请求已编码，调用的 deadline 为 `now + timeout`。任何接纳失败都不创建调用，也不产生后续终态回调。
2. owner 提交接纳时已编码的 snapshot，其中包含客户端 instance 和 operation 身份。请求成功提交后，该调用才具备响应匹配资格。
3. 对端 route 验证 RPC metadata，**在业务解码和 handler 副作用前预留 reply 存储**，再解码请求并执行同步 handler。请求 view 是借用值；响应 value 是端点持有、仅供本次 handler 使用的 scratch。
4. handler 填写响应并返回 `0`。route 保留客户端 session/operation，编码响应并入队。非零有符号 status 则编码为不含业务 body 的业务拒绝。
5. sink 返回 `BUSY` 时，已编码响应留在队列中，等待资源活动后再次提交；不会为这份保留响应重跑 handler。独立收到的重复请求仍可再次执行，kernel 没有 replay cache。
6. 客户端 owner 用响应 ID、instance 和 operation 匹配等待中且已提交的调用。生成 completion 解码一次响应，执行带类型回调。回调进入前逻辑槽已释放，handle 已失效。有效但重复、过期或不匹配的响应不会完成其他调用。

owner 持续推进时，每个已接纳调用恰好交付一个终态回调。[结果参考](kernel-api-cn.md#terminal-results)定义成功、拒绝、本地错误、超时、取消和关闭。成功 view 仅有效到回调返回。要保留 string、bytes 和嵌套值，请调用公开的 `*_value_from_view` 转换，见[保存结果](tutorial-rpc-values-cn.md)。

如果 handler 已产生副作用，随后响应编码失败，对端报告本地错误，不会自动将此错误作为业务拒绝返回。客户端最终可能超时；副作用和应用恢复逻辑需要据此设计。

## 调度使用事件和调用方时间

`wl_kernel_step(core, now, budget)` 支持 `1..WL_KERNEL_BUDGET` 个事件，当前上限为 `16`。取得一批物理 completion、交付一个终态、尝试提交一次 TX、尝试处理一次 RX，各消耗一个事件。发现 reply 压力的 RX 尝试也计数；后续 pass 在 reply 存储可用前不再重试它。`BUSY` TX 尝试计数，且在 port 活动前不再重试。

每次调度迭代先处理物理 completion，再在 RX 前处理符合条件的终态。多个调用同时到期时，它们在预算允许范围内都先于 RX；小预算把剩余终态留给后续 pass。step 入口的 call-generation 快照使回调中新建的调用不参与本次终态扫描。回调可以提交或取消调用，包括取消另一个已有调用；递归 `step` 或 `close` 返回 `WL_ERR_REENTRANT`。回调应及时返回，以便 owner 推进其他工作。

RX/TX 偏好跨 pass 保留。一次 TX 提交后，就绪 RX 在下一次提交前获得机会，即使上一 pass 的全部预算用于 completion。空 RX 或因 reply 压力阻塞的 RX 不会阻止 TX；连续 RX 也不会移除 TX 的推进机会。只要外部 owner 持续运行且 I/O 有进展，`budget=1` 也能推进双向工作。

### Deadline 包含排队时间

owner 提供单调的模 `uint32_t` tick。timeout 范围是 `1..INT32_MAX` tick，从接纳时开始，包括排队和等待 busy 传输的时间。core 不指定实际时间单位。Host Executor 使用毫秒，确定性 loopback 示例使用循环 tick。自定义 owner 的接纳、step 和等待必须使用一致单位。

到期判断为 `(uint32_t)(now - deadline) <= INT32_MAX`，因此相等即到期，deadline 可以跨过 `UINT32_MAX`。时间观察必须在模半范围内：不能让调用超过 deadline 后 `INT32_MAX` tick 仍未被检查。在恰好 deadline 时，即使响应更早已到达，只要仍在 RX 中，timeout 就优先；这里不按 I/O 到达时间戳裁决。

## 跟随物理 TX 仍活动时的 timeout

假设在 tick `100` 接纳调用，timeout 为 `10`：

1. 在 `100`，sink 返回 `STARTED`，保留请求字节。
2. 在 `110`，owner `step` 交付 `TIMED_OUT`。handle 失效，逻辑调用槽释放，但已租用的请求 snapshot 保持不变。
3. 后续 `CONSUMED` 发布说明 port 的源字节已不再被读取。kernel 仍把 snapshot 保留到 `DRAINED`；源已消费不允许启动第二笔物理 TX。
4. 实际 `DRAINED` 后，或真实完成/取消后发布 `TX_ERROR`，owner `step` 才释放物理 flight 和 snapshot。迟到响应不能再为已过期 operation 交付终态。

取消也具有相同的逻辑/物理生命周期分离。timeout 和 cancel 都是本地结果，不能停止对端副作用。重试创建新的 operation。重复执行副作用存在风险时，应用应定义请求 ID 和幂等性。长任务可以先返回 job ID，再查询进度，见[应用 job](tutorial-jobs-cn.md)。

## 背压沿存储传播

所有 reply snapshot 都被占用时，route 保留当前 RX borrow，不执行其业务 handler。RX 是 FIFO，保留的队首包也会延迟后续包。两个 RX unit 都被占用后，再次 claim 返回 `WL_ERR_WOULD_BLOCK`。

流式 feed 报告已消费的输入长度。遇到压力时，保留未消费后缀，空间可用后重试。可选的 SPSC byte FIFO 负责暂存字节，容量不要求为 2 的幂。普通压力暂停/恢复输入；实际丢字节则需要明确报告 gap 并重新同步流，见 [`packet_stream.h`](../include/wirelink/packet_stream.h) 和 [`byte_fifo.h`](../include/wirelink/byte_fifo.h)。

native USB 的 RX 还需要严格 read headroom、正确的 MPS/ZLP 处理，以及边界故障后的真实 reset/reconfigure。UART 的源消费与实际发射器 drain 分离。这些物理要求放在 [adapter 配置](adapters-cn.md)，不应折入应用 timeout 逻辑。

## 等待时不丢失活动通知

`wl_kernel_ready` 回答 core 是否有可运行工作，并报告最早的未来客户端 deadline。它不 service adapter，也不执行 OS 等待。外层循环必须：

1. service adapter，以时间快照 step kernel。
2. 重新采样时间，合并 core readiness、adapter readiness、应用工作/deadline。任何工作可运行时立即继续。
3. 使用锁存事件，保留查询与等待之间到达的通知；跨越该转换时复查工作/活动，不能清掉新到达的唤醒。
4. owner 工作后重新计算有限等待的剩余时间。host runtime 的 adapter 等待提示使用毫秒，与其 core tick 一致。

port activity bit 使阻塞的 TX 可以重试，但它本身不是 OS 等待原语。`on_error` 或终态计数也不是物理完成判据。[`Executor::run`](../runtime/host/src/executor.cpp) 提供可运行的 host 循环。

Executor 的外部命令队列有 32 槽，每批最多执行八条命令，再推进 kernel。命令入队和其中 RPC 的接纳是两次独立的容量判断。Session/SDK wrapper 提供 owner，并拥有复制后的应用结果，不另建 kernel 调用表。见 [Session](host-runtime-cn.md) 和 [SDK 示例](../examples/06_bindings/README.md)。

## 先关闭、结束 I/O，再释放存储

1. 在 owner 上调用 `wl_kernel_close`，停止 port 接纳、丢弃未租用输出、将等待调用标记为 `CLOSED`；已待交付的终态保持不变。close 安排回调，不内联执行回调。
2. 停止实际 adapter。取消/join 驱动操作、归还 claim 或 DMA 缓冲区；只有 adapter 确实停止使用源字节后，才能发布带 token 的最终 completion。部分 adapter 需要继续 service，才能达到异步 stop acknowledgement。
3. 继续 owner `step`，交付终态、丢弃排队 RX、取得最终 TX completion，直到 `wl_kernel_quiescent`。
4. 同时要求 adapter stop/join acknowledgement 和回调/worker 退出；之后才能释放端点、port、RX、stream、驱动和唤醒存储。

kernel quiescence 描述 core/port 状态，不代表外部线程已经退出。owner 回调可以请求外层停止，但不能 join 或销毁自己的 Executor/Session。外部 Session close 等待提交者结束，并 join I/O/owner 后才释放 C 存储。

重启必须在上述关闭完成后使用新的非零 instance 初始化。call operation/generation 与 packet 身份计数器耗尽时报告错误，不回绕；`WL_ERR_ID_EXHAUSTED` 要求执行同样的安全重建。旧 handle 和 completion token 不得用于新 instance。固定宽度结果类型和 C/C++ 存储边界见 [API 参考](kernel-api-cn.md#types-and-abi)。

## 继续阅读

- [Kernel 与 packet-port API](kernel-api-cn.md)：签名、错误和所有权。
- [RPC 调用](tutorial-rpc-cn.md)、[并发调用](tutorial-rpc-async-cn.md)、[保存结果](tutorial-rpc-values-cn.md)：应用任务。
- [集成](tutorial-integration-cn.md)和 [adapter](adapters-cn.md)：owner loop 与物理传输合同。
- [`kernel.c` 测试](../tests/core/kernel.c)、[`packet_port.c` 测试](../tests/core/packet_port.c)和[开发指南](development-cn.md)：实现和检查。
