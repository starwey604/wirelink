# Kernel 与 packet-port API

[English](kernel-api.md) · [执行模型](kernel-cn.md) · [Adapter 生命周期](adapters-cn.md)

应用消息和 RPC 使用生成的带类型入口；实现传输时使用 packet port。本参考覆盖 [`kernel.h`](../include/wirelink/kernel.h) 和 [`packet_port.h`](../include/wirelink/packet_port.h) 的全部操作，包括 route helper。[Wire 原语](../include/wirelink/kernel_wire.h)和 [connector](../include/wirelink/packet_stream.h)另有接口。

查找操作：

- Kernel：[初始化](#初始化-kernel)、[call](#接纳-rpc)、[cancel](#取消调用)、[send](#接纳普通消息)、[step](#推进-owner-工作)、[ready](#查询-readiness-与-deadline)、[close/quiescent](#关闭并查询-quiescence)、[route helper](#generated-route-操作)。
- Port：[初始化](#初始化-packet-port)、[生产 RX](#生产-rxclaimcommit-或-abort)、[消费 RX](#消费-rx-并查询空间)、[提交 TX](#提交物理-tx)、[completion](#发布并取得-tx-completion)、[activity](#活动与唤醒通知)、[stop](#停止-port-并确认静默)。

## 执行上下文与共同前提

- **Owner：**串行执行所有 kernel 操作、RX borrow/release、TX submit/take 和 owner 侧查询。业务 handler、completion 回调及 `on_error` 在 `wl_kernel_step` 中于此执行。
- **RX producer：**串行执行 claim/commit/abort。claim 在 commit 前是私有可写存储；owner 只看见已 commit 的包。
- **I/O publisher：**发布带 token 的 TX completion 和资源活动。发布可以在 sink 提交期间、返回之前发生。
- **初始化：**在启动 I/O 前执行。重新初始化要求 producer 已停止，没有未结束的 claim/borrow/物理租约，并使用新的身份。

除下文错误条件明确接受空指针外，调用方必须提供有效且已初始化的对象和有效输出指针。查询函数通常不检查参数。发布的指针、回调上下文和底层存储必须在操作生命周期内保持有效。复制活动 kernel、port 或原子对象不会创建独立端点。即使 C 暴露了存储布局，结构字段也是私有状态；不得直接修改字段推进状态机。owner 操作没有锁：`WL_ERR_REENTRANT` 不是并发访问检测器，也不允许多线程调用。

<a id="types-and-abi"></a>
## 类型与 ABI

`wl_err_t`、`wl_kernel_outcome_t` 和 `wl_packet_submit_t` 是 `int32_t` 类型，具名常量通过 enum 定义。即使使用 `-fshort-enums`，它们的结果存储/返回 ABI 也不缩小。这不意味着私有布局或所有编译选项均与 ABI 无关。存储布局变化时，使用兼容的头文件、生成代码和工具链设置重新构建消费者。

`wl_kernel_result_t` 包含 `outcome` 和有符号 `status`。`wl_kernel_call_handle_t` 包含 `uint64_t instance`、`uint32_t generation`、`uint32_t operation` 和 `uint8_t slot`，应把它们整体视为 opaque identity。`wl_packet_token_t` 包含 `uint64_t instance` 和 `uint32_t sequence`。

C 持有 packet-port 和 byte-FIFO 的 C11 原子存储；它们的 C++ 声明是不透明的。请在 C 中分配/定义存储，通过指针跨越边界，不能用 `std::atomic` 替换 C 原子表示。生成端点存储本身不含 C 原子成员。见配套的 [`cpp_owner.c`](../tests/core/cpp_owner.c) 和 [`cpp_consumer.cpp`](../tests/core/cpp_consumer.cpp)。

<a id="capacities"></a>
## 容量

| 资源 | 上限 / 定义 |
| --- | --- |
| Outbound call | `WL_KERNEL_CALLS = 4` |
| Request snapshot | 四份，生命周期独立于逻辑调用槽 |
| Reply snapshot | `WL_KERNEL_REPLIES = 4` |
| 普通消息 snapshot | 一份 |
| 接纳 FIFO | `WL_KERNEL_TX_COUNT = 9` 个 snapshot 索引 |
| RX unit | `WL_PACKET_RX_SLOTS = 2` |
| 物理 TX | 每个 port 一笔活动传输 |
| Step budget | `1..WL_KERNEL_BUDGET`，当前为 `1..16` 个事件 |
| 调用 timeout | `1..INT32_MAX` 个调用方 tick |
| Call operation / 每槽 generation | 不回绕的 `uint32_t` 身份 |
| RX claim sequence | 到 `UINT32_MAX` 耗尽，包括已 abort 的 claim |
| TX attempt sequence | 到 `WL_PACKET_SEQUENCE_MAX = 0x0fffffff` 耗尽 |

到达 sink 的 TX 尝试消耗一次身份，即使返回 BUSY 或 FAILED。耗尽返回 `WL_ERR_ID_EXHAUSTED`；必须 close、stop/join、达到 quiescence，再以新的非零 instance 重建。环形游标可以回绕，身份计数器不可以。instance 不得与仍可观察到的工作发生碰撞，包括跨重启的工作；`wl_packet_init` 只检查非零，不检查全局唯一性。

## 初始化 kernel

```c
wl_err_t wl_kernel_init(wl_kernel_t *kernel,
                        const wl_kernel_config_t *config);
```

`config` 绑定已初始化的 `port`、非空 `route`、可选的 `on_error`、对应 `user` 上下文，以及九个可写 `tx_storage` span。索引 `0..3` 是请求帧，每份至少 17 字节；`4..7` 是响应帧，每份至少 21 字节；`8` 是普通帧，至少 4 字节。这些只是初始化下限，不保证容纳所有选定业务消息。各 span 必须有独立且有效的存储；初始化复制描述符，不复制底层字节。WLC 自动提供 codec 对应容量。

返回 `WL_OK`；kernel/config/port/route 为空、port instance 为零、span data 为空或低于最小容量时返回 `WL_ERR_INVALID_ARG`。本操作不初始化 port、不启动 I/O，也不验证 port 是否 running。它清空 kernel 状态并保留指针，不能对活动工作执行。

**带类型用法：**初始化 `port` 后调用 `demo_kernel_init(&endpoint, &port, &handlers, on_error, user)`，见[已检入示例](../examples/bare_metal_loopback.c)。生成 init 复制 handler 表，将静态 route/存储绑定到本操作。

## 接纳 RPC

```c
wl_err_t wl_kernel_call_start(
    wl_kernel_t *kernel, uint16_t request_id, uint16_t response_id,
    const void *value, wl_kernel_encode_fn encode,
    uint32_t now, uint32_t timeout, wl_kernel_complete_fn complete,
    void (*callback)(void), void *user, wl_kernel_call_handle_t *out);
```

**参数与所有权。**两个 ID 必须非零且不同。`value` 是仅在本操作期间借用的编码输入；底层接口能否传空值由 encoder 决定。`encode`、`complete`、`callback` 和 `out` 必须非空。`user` 保存到 completion，必须存活到回调返回。`now`、`timeout` 与 `step` 使用相同 owner tick，timeout 为 `1..INT32_MAX`。deadline 是模运算的 `now + timeout`，包括排队时间。

**成功。**`WL_OK` 表示完整请求 snapshot 已编码、waiting call 和 FIFO 项已创建，`*out` 标识该调用；它不代表 sink 已接纳 TX。输入可以立即修改或释放，不会内联执行终态回调。请求 snapshot 与逻辑槽独立，被 I/O 租用时可存活到终态之后。

**失败。**下列任何失败均不创建调用、不产生新有效 handle，也不产生后续终态回调；由调用方决定是否以及何时重试：

| 结果 | 条件 |
| --- | --- |
| `WL_ERR_INVALID_ARG` | 必填指针为空、ID 或 timeout 无效 |
| `WL_ERR_INVALID_STATE` | Kernel 已关闭 |
| `WL_ERR_WOULD_BLOCK` | 没有空闲调用槽或请求 snapshot |
| `WL_ERR_ID_EXHAUSTED` | Operation 计数耗尽，或可用调用槽的 generation 均耗尽 |
| Encoder 错误 | Encoder 返回错误；生成 wrapper 将 codec 失败映射为 `WL_ERR_CORRUPT_PAYLOAD` |
| `WL_ERR_BUF_TOO_SMALL` | Encoder 长度超出容量，或 metadata/frame 放不下 |
| `WL_ERR_PAYLOAD_TOO_LONG` | 编码后 native payload 超出协议上限 |

通过初始参数验证后，core 在后续失败检查前将 `*out` 清零。参数错误，包括带类型 wrapper 的提前参数错误，可能保持输出不变。仅在返回 `WL_OK` 时使用输出。编码失败可能已改写闲置 snapshot 字节，但不接纳工作、不消耗 call generation/operation。

**Owner 约束。**可以在 owner 上调用，包括在 handler 和终态回调中。应使用生成的带类型 start，不要手动擦除回调类型。以下 demo 用法假定 `endpoint`、`now` 和 `completed` 已设置：

```c
request_value_t request = {.has_input = true, .input = INT32_MIN};
wl_kernel_call_handle_t handle;
wl_err_t error = demo_kernel_execute_start(&endpoint, &request, now, 500U,
                                          completed, NULL, &handle);
/* On WL_OK, request is no longer borrowed; completion needs owner steps. */
```

<a id="terminal-results"></a>
### 终态结果

owner 持续推进时，每个已接纳调用恰好交付一个终态。回调进入前 handle 已失效。cancel 成功后，即使终态尚未交付，也不能再用该 handle 取消。

| `outcome` | `status` / 带类型 view |
| --- | --- |
| `WL_KERNEL_SUCCESS` | `0`；生成 completion 提供非空响应 view |
| `WL_KERNEL_REJECTED` | 远端非零有符号业务 status；view 为空 |
| `WL_KERNEL_LOCAL_ERROR` | 本地 `wl_err_t`，如 `WL_ERR_IO`、`WL_ERR_ID_EXHAUSTED` 或响应解码的 `WL_ERR_CORRUPT_PAYLOAD`；view 为空 |
| `WL_KERNEL_TIMED_OUT` | `WL_ERR_TIMEOUT`；view 为空 |
| `WL_KERNEL_CANCELLED` | `WL_ERR_CANCELLED`；view 为空 |
| `WL_KERNEL_CLOSED` | `WL_ERR_INVALID_STATE`；view 为空 |

RPC metadata 格式错误属于 route 错误，不会自动成为某调用的终态。已匹配成功响应的 body 若无法通过生成解码，则交付 `LOCAL_ERROR / CORRUPT_PAYLOAD`。有效但不匹配或过期的响应被忽略。timeout/cancel 不能撤销远端副作用；重试使用新 operation。

响应结构及所有指向 payload 存储的 view 只借用到回调返回。保留结果时用公开的 `*_value_from_view` 复制。[拥有结果的示例](../examples/02_device_info/client.c)通过[共享 completion helper](../examples/common/loopback_owner.h)完成该操作。

## 取消调用

```c
wl_err_t wl_kernel_cancel(wl_kernel_t *kernel, wl_kernel_call_handle_t handle);
```

将匹配的 waiting call 标记为 `CANCELLED`，由后续 `step` 交付回调。它不发送取消包，不停止物理 TX。尚未租用的排队请求在交付终态时移除；已租用字节保留到物理 drain。

匹配 waiting handle 时返回 `WL_OK`；kernel 为空返回 `WL_ERR_INVALID_ARG`；instance/slot/generation/operation 不符、已处于待交付终态或已完成时返回 `WL_ERR_NOT_FOUND`。重复 cancel 不安排第二次回调。仅限 owner，包括回调内部。最小用法是 `error = wl_kernel_cancel(&endpoint.core, handle);`，随后继续 step。

## 接纳普通消息

```c
wl_err_t wl_kernel_send(wl_kernel_t *kernel, uint16_t message_id,
                         const void *value, wl_kernel_encode_fn encode);
```

在返回 `WL_OK` 前，将输入同步编码到唯一普通 snapshot 并入队。随后源可复用，包括 view 引用的字节。普通发送没有终态回调。kernel/encoder 为空或 ID 为零返回 `WL_ERR_INVALID_ARG`；关闭后返回 `WL_ERR_INVALID_STATE`；snapshot 被占用时返回 `WL_ERR_WOULD_BLOCK`；其他编码/frame 错误与 call 接纳相同。失败不接纳工作。后续 TX 失败通过已配置的 `on_error` 报告。

仅限 owner，包括回调内部。最小带类型用法为 `error = demo_kernel_send_event(&endpoint, &event);` 或 `demo_kernel_send_event_view(&endpoint, &borrowed_event)`，两者均在返回前通过编码完成复制。见[普通消息过程](kernel-cn.md#跟随一条普通消息)。

## 推进 owner 工作

```c
wl_err_t wl_kernel_step(wl_kernel_t *kernel, uint32_t now, unsigned budget);
```

`budget` 是 `1..16` 个事件。一批 completion、一次终态交付、一次 TX 尝试或一次 RX 尝试各消耗一个事件，空闲检查不消耗。`now` 是本次调用始终使用的一次快照。物理 completion 先于符合条件的终态，终态先于 RX。generation 快照使回调中新建调用不参与本次终态扫描。多个到期调用在剩余预算范围内都先于 RX 交付。RX/TX 轮次跨 pass 保留，包括 `budget=1`。

deadline 相等即到期：`(uint32_t)(now - deadline) <= INT32_MAX`。它优先于该快照时刻仍排队的响应。使用一致、单调的模 tick、`1..INT32_MAX` timeout，并在模半范围内检查调用。见[时间和调度](kernel-cn.md#调度使用事件和调用方时间)。

执行工作未报告错误时返回 `WL_OK`；kernel 为空或 budget 无效返回 `WL_ERR_INVALID_ARG`；递归 step 返回 `WL_ERR_REENTRANT`；否则返回观察到的 I/O/route/frame 错误。错误不会回滚已执行工作，也不会关闭 kernel。已配置的 `on_error` 在 owner 上报告 route 和 TX 错误；单个返回值不是完整事件日志。一次正常 step 可交付多个回调，也可能没有回调。关闭后仍应继续 step 完成收尾。不得并发运行 step，也不得在回调中递归运行。

最小用法为 `error = wl_kernel_step(&endpoint.core, now, WL_KERNEL_BUDGET);`。`demo_kernel_step(&endpoint, now)` 使用该最大预算。

## 查询 readiness 与 deadline

```c
int wl_kernel_ready(const wl_kernel_t *kernel, uint32_t now,
                     int *has_deadline, uint32_t *deadline);
```

仅限 owner，所有指针必须有效。存在待处理 completion、符合条件的终态、可运行 RX 或排队 TX 时返回非零。因 reply 压力阻塞的 RX 要等容量恢复或 kernel 关闭才可运行。被 BUSY 阻塞的 TX 需要 port 活动；活动物理 TX 阻止再次提交。

始终设置 `*has_deadline`；为非零时，`*deadline` 是最早的未来客户端 deadline，否则其值未指定。到期调用使返回值为 runnable，而不贡献未来 deadline。不同工作可以同时导致 runnable 和未来 deadline。本函数不消费活动，也不保证 producer 接下来不会发布。

```c
int has_deadline;
uint32_t deadline;
int runnable = wl_kernel_ready(&endpoint.core, now, &has_deadline, &deadline);
/* Merge with adapter/application readiness; use a latched wake before wait. */
```

外层循环合同见[不丢通知的等待](kernel-cn.md#等待时不丢失活动通知)。只有对本次返回的未来 deadline 和同一时间采样，`deadline - now` 才是相对等待；执行更多工作后必须重新计算。

## 关闭并查询 quiescence

```c
wl_err_t wl_kernel_close(wl_kernel_t *kernel);
int wl_kernel_quiescent(const wl_kernel_t *kernel);
```

`close` 仅限 owner，在 step/回调之外调用。它停止 port，拒绝新接纳，丢弃排队/未租用输出，将 waiting call 标记为 `CLOSED`。已标记终态保留原结果，已租用输出保持有效。返回 `WL_OK`，重复 close 也如此；kernel 为空返回 `WL_ERR_INVALID_ARG`，step 内调用返回 `WL_ERR_REENTRANT`。它不内联执行终态回调。

`quiescent` 要求 owner 上有效的 kernel，仅在已关闭、不在 step 中、所有调用槽空闲且 port quiescent 时返回非零。它不确认驱动停止、不 join 外部 worker，也不证明回调已退出。

最小顺序：`wl_kernel_close(&endpoint.core)`，按 adapter 的实际合同 stop/join，继续 step 处理最终 completion，然后同时要求 `wl_kernel_quiescent(&endpoint.core)` **及** adapter 停止确认，才能释放存储。见[关闭顺序](kernel-cn.md#先关闭结束-io再释放存储)。

## 回调接口

```c
typedef wl_err_t (*wl_kernel_encode_fn)(const void *value, uint8_t *data,
                                       size_t capacity, size_t *length);
typedef void (*wl_kernel_complete_fn)(wl_kernel_t *kernel,
    const wl_kernel_call_t *call, wl_kernel_result_t result,
    const uint8_t *body, size_t length);
typedef wl_err_t (*wl_kernel_route_fn)(wl_kernel_t *kernel,
                                      const wl_kernel_frame_t *frame);
typedef void (*wl_kernel_error_fn)(void *user, uint16_t message_id, wl_err_t error);
```

- **Encode：**在 owner 上同步执行，不得写出 capacity，成功时设置 length，不保留输入/输出指针。底层 encoder 错误向上传播。
- **Complete：**生成 glue 接收保存的 call 描述符，解码一次成功 body，将已擦除回调转回原始函数指针类型后调用，不通过擦除类型执行。保存的描述符和 body 仅在本次调用中借用。
- **Route：**仅在 owner step 中执行，分发借用 frame。`WL_OK` 消费 RX；`WL_ERR_WOULD_BLOCK` 保留 RX 以便重试，因此只能在 handler 副作用前返回它，且不得保留 reply reservation。其他错误释放 RX 并报告。生成 route 在业务解码/handler 副作用前 reserve。
- **Error：**接收配置的上下文、消息 ID 和本地错误；包解析失败时 ID 可能为零。它不表示业务拒绝，也不独立完成 outbound call。

handler/completion/error 回调可以在同一 owner 上 send、start 或 cancel。递归 step/close 被拒绝。回调应保持非阻塞；owner 回调不得 join/销毁自己的 runtime。[生成 demo route](../examples/kernel/generated/demo_kernel.c)同时演示这四个接口。

## Generated-route 操作

这些是供生成 dispatch 使用的 owner-step helper，不是应用可保留的延迟回复 token。长任务使用同步生成 handler 加应用 job。

```c
wl_err_t wl_kernel_reply_reserve(wl_kernel_t *kernel, unsigned *index);
void wl_kernel_reply_abort(wl_kernel_t *kernel, unsigned index);
wl_err_t wl_kernel_reply_finish(wl_kernel_t *kernel, unsigned index,
    uint16_t response_id, const wl_kernel_rpc_metadata_t *request,
    int32_t status, const void *value, wl_kernel_encode_fn encode);
wl_err_t wl_kernel_response(wl_kernel_t *kernel, uint16_t response_id,
                            const uint8_t *data, size_t length);
```

**Reserve：**在 step 内、业务解码或 handler 副作用前调用。`WL_OK` 将一个 reply snapshot 标记为 building，并写出索引 `4..7`。`WL_ERR_WOULD_BLOCK` 表示没有 reply 存储，应保留 RX，不运行 handler。kernel/输出为空、在 step 外或 kernel 已关闭时返回 `WL_ERR_INVALID_STATE`。失败后的输出索引不可用。

**Abort：**reserve 成功后，解码失败时释放 reservation。要求有效 kernel 和 owner-step 上下文；不处于 building 的 reply index 被忽略。没有回调，不得跨 route 调用保留索引。

**Finish：**使用已 reserve 的 index 和已验证的 request metadata，响应 ID 必须非零。`status=0` 用非空 `encode` 编码 `value`；非零 status 编码拒绝，不调用 encoder，也不添加业务字节。`WL_OK` 将拥有存储的 reply snapshot 入队并结束 reservation，返回后不再借用 metadata/value。kernel/request 为空、ID/index 无效、step 外、index 非 building，或成功响应 encoder 为空时返回 `WL_ERR_INVALID_ARG`。参数错误不会释放有效 reservation，route 必须 abort。metadata/编码/frame 错误则释放 reservation 并返回错误，不自动生成错误响应。

**Response：**在 owner step 内分发响应 ID 的 payload。在 step 外返回 `WL_ERR_INVALID_STATE`，metadata 格式错误返回解析错误。有效但 session 错误、尚未发送、非 waiting 或 operation/响应 ID 不匹配的包返回 `WL_OK`，不执行回调。匹配则释放逻辑调用，并在 step 内携成功/拒绝和借用 body 执行 completion。

最小 route 模式：验证 metadata、reserve、decode、解码失败则 abort、执行 handler、finish；响应分支调用 `wl_kernel_response`。[已检入 route](../examples/kernel/generated/demo_kernel.c)是完整实现。[`kernel.c` 测试](../tests/core/kernel.c)覆盖 reservation 压力、重复请求和副作用后的响应编码失败。

## 初始化 packet port

```c
wl_err_t wl_packet_init(wl_packet_port_t *port, const wl_packet_config_t *config);
size_t wl_packet_rx_unit_size(const wl_packet_port_t *port);
```

`config` 提供新的非零 `instance`、`rx_storage`、`rx_unit_size`、`rx_storage_size`、非空 `submit`、可选 `notify` 和共享 `user`。存储至少容纳两个 unit；每个 unit 必须放得下 connector 可能发布的最大包，物理 adapter 可能还需要额外 headroom。init 复制配置描述符并启动 port 接纳，不启动驱动。底层存储/上下文仍由调用方持有。

port/config/RX data/submit 为空、instance 或 unit size 为零、两份 unit 容量溢出或总存储不足时返回 `WL_ERR_INVALID_ARG`。`WL_ERR_NOT_SUPPORTED` 表示所需 C 原子不是 lock-free，port 未 running，不能当作初始化成功使用。其他情况返回 `WL_OK`。init/reinit 不得与 I/O 竞争，也不得抹去活动 claim、borrow 或 transfer。

`wl_packet_rx_unit_size` 返回配置的 unit size；port 为空时返回 `0`，不 claim 空间。最小初始化见 [`bare_metal_loopback.c`](../examples/bare_metal_loopback.c)，init 后用 `wl_packet_rx_unit_size(&port)` 查询容量。

## 生产 RX：claim、commit 或 abort

```c
wl_err_t wl_packet_rx_claim(wl_packet_port_t *port, size_t maximum,
                            wl_packet_claim_t *out);
wl_err_t wl_packet_rx_commit(wl_packet_port_t *port,
                             const wl_packet_claim_t *claim, size_t length);
wl_err_t wl_packet_rx_abort(wl_packet_port_t *port,
                            const wl_packet_claim_t *claim);
```

该序列由一个串行 RX producer 持有。**Claim** 返回恰好 `maximum` 字节的可写 `out->span` 和新 token，只能在其范围内写入；commit 前 owner 不可见。即使调用方修改本地 claim 描述符，原始保存容量仍是验证依据。

| Claim 结果 | 含义 |
| --- | --- |
| `WL_OK` | Producer 持有一个可写 claim |
| `WL_ERR_INVALID_ARG` | port/输出为空或 maximum 为零 |
| `WL_ERR_INVALID_STATE` | Port 已停止 |
| `WL_ERR_BUSY` | 已有活动 producer claim |
| `WL_ERR_BUF_TOO_SMALL` | Maximum 超过 RX unit size |
| `WL_ERR_WOULD_BLOCK` | 两个 unit 均被占用 |
| `WL_ERR_ID_EXHAUSTED` | Claim 身份耗尽 |

初始参数验证后，claim 在其他检查前将输出清零；仅在 `WL_OK` 时使用。**Commit** 要求长度为 `1..claimed maximum`，以 release 顺序发布 length/data 并触发活动。`WL_OK` 结束 producer 所有权，直到取得新 claim 前不得写入。**Abort** 释放匹配 claim，不发布输入，并触发活动。

commit/abort 在 port/claim 为空时返回 `WL_ERR_INVALID_ARG`，在 instance/sequence/data pointer 过期或不匹配时返回 `WL_ERR_NOT_FOUND`。commit 的零长度/超长也返回 `WL_ERR_INVALID_ARG`，claim 保持活动，必须修正或 abort。stop 后有效 commit 会丢弃 claim、触发活动并返回 `WL_ERR_INVALID_STATE`，不发布包。stop 后仍允许 abort；已经完成的 claim 不能复用。

最小 producer 用法，假定 `length` 非零且 `source` 可读：

```c
wl_packet_claim_t claim;
wl_err_t error = wl_packet_rx_claim(&port, length, &claim);
if (error == WL_OK) {
  memcpy(claim.span.data, source, length);
  error = wl_packet_rx_commit(&port, &claim, length);
  /* If a validation error leaves the claim active, correct it or abort it. */
}
```

[Loopback service](../src/packet_loopback.c)也演示了失败时 abort。

## 消费 RX 并查询空间

```c
wl_err_t wl_packet_rx_borrow(wl_packet_port_t *port, wl_packet_borrow_t *out);
wl_err_t wl_packet_rx_release(wl_packet_port_t *port,
                              const wl_packet_borrow_t *borrow);
int wl_packet_rx_ready(const wl_packet_port_t *port);
int wl_packet_rx_available(const wl_packet_port_t *port);
```

**Borrow** 仅限 owner，返回最早 commit 包的只读 data/length/token。`WL_OK` 开始借用；port/输出为空返回 `WL_ERR_INVALID_ARG`，已有 owner borrow 返回 `WL_ERR_BUSY`，队列为空返回 `WL_ERR_NO_DATA`。失败不产生新 borrow。**Release** 验证 instance/sequence/data pointer，推进 RX 并触发活动；在它完成前 producer 不得复用该 unit。release 返回 `WL_OK`，参数为空返回 `WL_ERR_INVALID_ARG`，不是当前 borrow 返回 `WL_ERR_NOT_FOUND`。两者都不要求 port running，可在 stop 后清空已 commit 输入。使用者仍在读字节时不得 release。

`rx_ready` 是 owner 查询，表示已 commit 输入，包括当前 borrow。`rx_available` 是 producer/owner 提示，stop、已有活动 claim 或两个 unit 被占用时为假。真值不预留 unit，以下一次 claim 为准。两者都要求有效已初始化 port，返回布尔值。

最小 owner 模式：`wl_packet_rx_ready(&port)` 为真时 borrow，同步处理 borrow，再 `wl_packet_rx_release(&port, &borrow)`。接入 kernel 后由 kernel 持有 consumer 侧，adapter 不得另行 borrow。就绪/可用及过期 release 用例见 [`packet_port.c` 测试](../tests/core/packet_port.c)。

## 提交物理 TX

```c
typedef wl_packet_submit_t (*wl_packet_sink_fn)(void *user,
    const uint8_t *data, size_t length, wl_packet_token_t token);
wl_err_t wl_packet_tx_submit(wl_packet_port_t *port, const uint8_t *data,
                             size_t length, wl_packet_token_t *out);
```

仅限 owner，要求 port/data/输出非空且 length 非零。port 在同步调用配置 sink 前建立 active token，支持内联 `wl_packet_tx_publish`。sink 必须返回以下之一：

| Sink 结果 | 接纳和后续义务 |
| --- | --- |
| `WL_PACKET_SENT` | 源已消费**且**传输已物理 drain；port 自动发布 DRAINED |
| `WL_PACKET_STARTED` | 已接纳传输；消费前保留源，使用该 token 发布 completion，可在返回前发布 |
| `WL_PACKET_BUSY` | 未接纳、不保留源，不允许 completion；资源可用时发布活动 |
| `WL_PACKET_FAILED` | 未接纳、不保留源，不允许后续 completion |

SENT/STARTED 使 port 返回 `WL_OK` 和有效 `*out`；TX 已活动或 sink BUSY 返回 `WL_ERR_WOULD_BLOCK`；sink FAILED 返回 `WL_ERR_IO`；stop 后返回 `WL_ERR_INVALID_STATE`；TX 身份耗尽返回 `WL_ERR_ID_EXHAUSTED`；参数无效返回 `WL_ERR_INVALID_ARG`。失败不写入新输出 token。owner 通过 completion 观察到 CONSUMED 前，不得覆盖已接纳源内存。owner take DRAINED 前不得启动第二笔传输，即使 sink 已内联完成。

最小直接 port 用法：提交有效源，`WL_OK` 时保存 token，再按下节取得 completion。接入 kernel 时由 kernel 持有 TX submit/take，adapter 实现 sink 和发布，不另建提交者。见 [TX fixture](../tests/core/packet_port.c)。

## 发布并取得 TX completion

```c
wl_err_t wl_packet_tx_publish(wl_packet_port_t *port,
                              wl_packet_token_t token, unsigned flags);
unsigned wl_packet_tx_take(wl_packet_port_t *port, wl_packet_token_t *out);
int wl_packet_tx_active(const wl_packet_port_t *port);
int wl_packet_tx_ready(const wl_packet_port_t *port);
```

**Publish** 在 I/O 上下文执行，也允许在 sink 内内联执行。flags 可按位或组合，表示真实生命周期边界，不表示 RPC 逻辑状态：

| Flag | 含义 |
| --- | --- |
| `WL_PACKET_CONSUMED = 2` | Adapter 不再读取源字节 |
| `WL_PACKET_DRAINED = 4` | 物理传输结束，隐含 CONSUMED |
| `WL_PACKET_TX_ERROR = 8` | 传输失败/取消结束，且不再使用源；隐含 DRAINED 和 CONSUMED |

活动匹配 token 上新增 bit 时返回 `WL_OK`；port 为空、flags 为零或含未知位返回 `WL_ERR_INVALID_ARG`；instance/sequence 错误、传输不活动/已 drained，或没有新增 bit 的发布返回 `WL_ERR_NOT_FOUND`。sequence 和 flags 位于同一 lock-free 原子字中，过期 publisher 不会将 completion 附到新传输。stop 不拒绝现有 TX 的有效最终 completion。

**Take** 仅限 owner，要求有效 port/输出。每个新 bit 只返回一次，仅在非零结果时写入匹配 token；`0` 表示没有新 bit，输出不变。take DRAINED 结束 port 的活动传输。如果此前已 take CONSUMED，后续 DRAINED 不必再次包含 CONSUMED。kernel 保守地将已租用 snapshot 保留到 DRAINED。

`tx_active` 要求有效 port，表示物理 port 占用，包括已发布但尚未 take 的 DRAINED。它是安全的原子状态观察，本身不结束租约。`tx_ready` 仅限 owner，表示是否有尚未 take 的 completion bit。两者返回布尔值，不推进状态。

最小 publisher 用法：实际 drain 后调用 `wl_packet_tx_publish(&port, token, WL_PACKET_DRAINED)`。最小直接 owner 用法：`flags = wl_packet_tx_take(&port, &token)`，在 CONSUMED 后释放源，在 DRAINED 后允许下一笔 TX。接入 kernel 则继续 step。[Port 测试](../tests/core/packet_port.c)覆盖分步和内联 completion，[并发 fixture](../tests/core/packet_port_concurrent.c)让旧 completion 与新提交竞争。

## 活动与唤醒通知

```c
typedef void (*wl_packet_notify_fn)(void *user);
void wl_packet_activity(wl_packet_port_t *port);
int wl_packet_take_activity(wl_packet_port_t *port);
int wl_packet_activity_pending(const wl_packet_port_t *port);
```

均要求有效已初始化 port。`activity` 可在 producer/I/O 或 owner 上运行，锁存 activity bit，存在配置时调用 `notify(user)`。notify 可以在 I/O 上下文执行，必须非阻塞，不得重入 owner 操作。RX commit/abort/release、成功 TX 发布和 stop 已自动触发活动。sink 返回 BUSY 后必须在资源可用时触发活动，否则 kernel 会保持 TX 重试阻塞。

`take_activity` 是 owner consumer，原子清除并返回此前锁存 bit。kernel 在 step 入口用它重新启用 TX 重试，外层 runtime 不得抢走该 consumer 的活动。`activity_pending` 是 producer/owner 可用的非破坏性原子观察。这些都是合并的提示，不是事件计数、业务回调或物理 completion。

最小 adapter 用法：BUSY 资源恢复时 `wl_packet_activity(&port)`。将 `notify` 接到外部锁存事件，并跨查询/等待结合 `activity_pending`/readiness 使用。见[等待](kernel-cn.md#等待时不丢失活动通知)。

## 停止 port 并确认静默

```c
void wl_packet_stop(wl_packet_port_t *port);
int wl_packet_quiescent(const wl_packet_port_t *port);
```

`stop` 仅限 owner，要求有效 port；kernel close 也会调用它。它原子停止新 claim/submit 并触发活动，允许重复 stop。它不取消驱动、不 abort 活动 claim、不消费排队 RX，也不 take TX completion。已持有 claim 的 producer 必须 commit/discard 或 abort；活动 TX 仍需真实最终发布。stop 必须与 adapter 关闭协调，不能代替 producer 退出。

`quiescent` 是 owner 查询，要求接纳已停止、没有活动 claim、没有 borrow、没有排队 RX、没有活动 TX；返回布尔值，不返回错误。它不能证明通知回调或驱动 worker 已退出。存储/上下文还必须存活到 adapter stop/join acknowledgement。接入 kernel 时用 `wl_kernel_quiescent` 加上逻辑调用要求。

最小直接 port 关闭：stop，同步 producer 并完成其 claim/transfer，释放所有借用/排队 RX，take 最终 TX completion，确认 quiescence 后以新 instance 重建。[Stop/reinit fixture](../tests/core/packet_port.c)覆盖 stop 后最终 completion、新 instance 和身份耗尽。
