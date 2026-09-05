# 无动态分配的 RPC Runtime

> 英文版 [`rpc-runtime.md`](rpc-runtime.md) 是规范来源。

`wirelink/rpc.h` 在不修改 Wirelink v1 帧头的前提下关联请求与响应。新应用优先使用
[RPC 教程](tutorial-rpc-cn.md)中的生成端点；本文先说明托管 RPC 的通信与所有权边界，
后半部分是高级底层接口。

## 托管 RPC 与已有协议映射（生成 ABI 20）

在 `.bind.wl` 中同时省略 `request_operation_id`、`response_operation_id`、
`response_status`，即选择托管 RPC。`.wl` 只定义业务参数；runtime 自动分配调用编号、
检查响应类型并补齐回复元数据。业务 codec 和 schema identity 不依赖这些运行策略。

三个映射全部写出，则选择已有的映射模式，保留原有 payload 编码和 profile identity。
请求、响应的字段名称可以不同，同一次调用携带的编号值必须相同。只写一部分映射会编译失败，
接收端不会自动探测或回退到另一种格式。

托管 RPC 在普通业务编码前加上固定的 12 字节：

| 偏移 | 字节数 | 内容 |
| --- | --- | --- |
| 0 | 1 | `0x00`，不是合法的旧 codec 字段 tag |
| 1 | 1 | 元数据版本 `1` |
| 2 | 1 | 请求为 `1`，响应为 `2` |
| 3 | 1 | 保留，必须为零 |
| 4 | 4 | 非零调用编号，无符号大端整数 |
| 8 | 4 | 业务状态，有符号 32 位补码、大端整数 |

请求状态为零；成功响应状态为零，后接业务响应；拒绝响应状态非零，且**不携带业务响应体**，
即使响应 schema 有必填字段也不用伪造值。前缀错误、编号为零、请求状态非零、拒绝响应携带
额外内容均报 `WL_RPC_ERR_MALFORMED_METADATA`。成功响应体仍由 codec 验证。元数据不提供认证。

双方必须选择相同模式。将既有服务从映射改为托管会改变 payload 字节，即使 DATA 消息编号
没变，也要同步升级两端，或为新格式使用不同消息编号。模式和元数据版本进入 profile identity。
仅把 schema 的 `= n` 改成 `@id(n)` 则不改变标识摘要或字节。

## 默认调用与回复的所有权

每个服务生成 `*_call_t`、`*_result_t`、`*_request_token_t`。
`endpoint_*_call()` 返回句柄，`*_inspect()` 返回状态和类型化结果，`*_release()` 回收终态调用。
拒绝时 `response_valid=false`，`application_status` 非零。`*_cancel()` 停止本地等待并尝试
取消对应链路发送，不会撤销远端业务；失败、超时、取消也必须释放。

端点 call/complete/reject 统一返回 `wl_rpc_err_t`，详细 codec／链路诊断可从
`endpoint_result()` 读取；高级 runtime 函数仍保留 tagged result。
在推进回调内准备回复时，即使回复成功，也不会抹掉本轮较早出现的错误。

句柄检查端点归属、端点代次、服务消息类型和客户端槽位代次，防止旧句柄释放新调用。
回复 token 检查 runtime 归属、代次和服务端执行凭据；异步业务可以复制它，不要拆解内部字段。
默认端点关闭／重新初始化后，旧句柄与 token 均失效。自定义 runtime 集成需要在重建时
丢弃旧 token，或自行维护 `rpc_incarnation`。

handler 返回零表示本地正常接手（也允许稍后完成）；用 `*_complete()` 回复成功，
用 `*_reject(..., 非零状态, now)` 回复业务拒绝。handler 返回非零表示本地放弃，
不会自动发送业务拒绝。请求里的借用字段只在回调期间有效；响应中的借用字段（若 schema
允许）有效到调用释放，需要更久就复制其内容。

并发响应按调用匹配，不要求按发起顺序到达。未知／已释放调用的回复、已到终态调用的回复，
托管 runtime 会忽略并返回 `RUNTIME_OK`；可选 `on_result` 仍能从 `detail.rpc.rpc_result`
读到 `NOT_FOUND` 或 `INVALID_STATE`，但不会让另一条调用失败。畸形元数据、错误响应类型和
codec 错误仍作为明确的分发错误报告。

默认端点只有一个客户端槽；需要并发时使用自定义 runtime 存储。首次由编号捕获句柄需要扫描，
后续底层句柄查询／取消／释放为 O(1)。底层句柄仅在单次 client 初始化生命周期内有效。
代次状态仍装在原有的 64 字节 client／slot 存储中，不引入全局计数器、堆、线程或时钟。
纯托管 runtime 不再分配旧的类型化编码暂存区：请求直接写入链路 TX claim，响应直接写入
预留的缓存区。链路与响应容量自动包含 12 字节前缀；请求指纹只覆盖业务规范编码，在本地计算，
不作为字段发送。

## 调用关联不等于业务幂等

每次托管 `call()` 都是新操作，没有让普通用户指定内部编号来重试的入口。
服务端会在配置的会话／缓存／过期范围内抑制重复通信请求并重放响应，但相同参数发起的新调用
可以再次执行。需要跨重试或持久化幂等时，应显式设计业务键和状态机。

本地句柄代次不会增加线上会话字段。数值编号复用后，无法单凭编号识别无限期迟到的旧响应。
重建客户端时需要排空／重置传输，避免旧响应仍可能存在时复用编号；当前格式不保证跨重启的
响应新鲜度。可靠对端会话观察限定的是服务端请求重放范围，不是永久“只执行一次”或客户端
跨重启新鲜度。不可靠请求没有可靠对端的会话分区。这些限制同样适用于映射模式。

## 底层分配与调度

runtime 无 heap、无隐藏锁。client slot、server pending/cache slot 和有界 response
字节存储均由调用方提供。所有调用属于拥有 `wl_ctx_t` 的同一 consumer；生成 handler
可把 response 路由到该 consumer，但不得递归调用 context。

## 底层 Client 生命周期

`wl_rpc_client_begin()` 保留 slot 并启动端到端 deadline，覆盖 `QUEUED`、
`LINK_PENDING` 和 `WAIT_RESPONSE`。零表示不超时；非零值必须小于 `2^31` ms，保证
wrap-safe。

client/server 的 `*_get_deadline_hint()` 无副作用，返回最近的相对 deadline：到期为
`0`，不存在为 `UINT32_MAX`。consumer 可与 `wl_poll_get_hint()` 取最小值；只有
`poll()` 推进 RPC deadline。就绪的 server response 返回零，要求 owner 在休眠前处理。

可靠 request 编码发送后，用 `wl_rpc_client_bind_tx()` 绑定 `wl_tx_handle_t`，并把终态
TX event 交给 `wl_rpc_client_on_tx_event()`。`WL_EVT_TX_SUCCESS` 只进入
`WAIT_RESPONSE` 并设置 `link_delivery_confirmed=1`，不会完成 RPC。底层 API 调用者还要
`wl_tx_take()`；WLC runtime 匹配事件后会同时完成两步，此时不要重复 take。

不可靠 request 或无可关联 handle 的 wrapper，在本地提交成功后调用
`wl_rpc_client_tx_completed()`，以 `link_delivery_confirmed=0` 进入等待。处于
`LINK_PENDING` 时收到精确 response 也可完成 RPC，因为应用响应比 ACK/TX 事件顺序更
强；保留的 TX handle 仍需独立 cancel 或 drain/take。`QUEUED` 状态尚未发送时收到的
response 会被拒绝。

response 必须精确匹配 operation ID 和 response message ID。字节复制进 slot 的固定
segment，保持到 `wl_rpc_client_release()`。应用 status 为零进入 `COMPLETED`，非零进入
`APPLICATION_ERROR`；过大 response 也进入应用错误，并设置
`WL_RPC_ERR_RESPONSE_TOO_LARGE`。

cancel 只停止本地等待。先 inspect 取出绑定 handle，再按需 `wl_tx_cancel()`；晚到
completion 会被拒绝。timeout 同样不会释放 core transaction，最终仍必须在终态后
`wl_tx_take()`。终态 slot 和 operation ID 在显式 release 前一直保留。

自动 ID 单调递增并跳过本地仍保留的 slot。映射模式的生成 client start 会使用 request 中已有的
非零 ID，否则自动分配。release 后，用相同 ID 和 canonical request 重试可命中 server
的有界 replay cache；相同 ID 搭配不同数据会在 cache 存续期间冲突。

## Server 去重与 Replay

`wl_rpc_server_begin()` 按可靠 sender 的 session 划分 operation ID，比较 peer
session ID、operation ID、request/response message ID 和调用方生成的 canonical
fingerprint。fingerprint 的碰撞质量由产品/schema 负责。零 peer session 表示不划分
session。

- `NEW`：原子保留 pending metadata 和一个 response-cache slot，然后才允许执行
  handler；任何 pool 满都不会调用 handler。
- `PENDING_DUPLICATE`：抑制相同活动 request 的重复执行。
- `REPLAY`：返回有界缓存 response，供立即重发。
- `CONFLICT`：同一 peer session 内以相同 operation ID 搭配不同 identity；新 session
  中同一 ID 仍为 `NEW`。

`complete()`、`reject()`、`abandon()` 接收 `begin()` 返回的完整
`wl_rpc_server_request_t`。带 generation 的 identity 允许不同 session 使用相同数字
ID，并阻止旧异步 completion 命中新复用的 slot。终态转换或 session discard 后 token
失效。

`WL_RPC_CACHE_REJECT_NEW` 在 cache 满时于 handler 前拒绝；
`WL_RPC_CACHE_EVICT_OLDEST` 可保留最老且已经 delivered 的 generation。等待提交或可靠
完成的 entry 不可淘汰。response storage 已在 begin 时保留，所以合法 completion 不会
因其他 operation 填满 cache 而失败。

## Response 所有权与推进

生成 response 可用 `wl_rpc_server_response_prepare()` 借用 begin 时预留的 cache
segment，原地 encode，再用 `wl_rpc_server_response_commit()` 发布前缀，避免第二份
response 大小的 scratch 和 copy。codec 失败后 request 仍 pending。commit、reject、
abandon、expiry 或 session discard 会结束 borrow。已有外部字节可使用复制式
`complete()`/`reject()`。

completion 只让自有字节 ready，不调用 link。owner 通过
`wl_rpc_server_response_acquire()` 获取一个 response，然后必须恰好执行一种转换：

- 同步背压：`wl_rpc_server_response_defer()`；
- 已接受可靠 TX：带 handle 调用 `wl_rpc_server_response_submitted()`；
- 已接受不可靠 TX：`wl_rpc_server_response_sent()`。

可靠终态事件交给 `wl_rpc_server_on_tx_event()`。任何终态都结束本次有界链路尝试，并
留下可 replay entry；duplicate 会把同一份自有字节重新置为 ready。RPC 不在 link ARQ
之上制造无界重试。acquire 的字节在匹配转换前保持稳定。

WLC 的 `*_runtime_service()` 实现上述序列，同时推进 deadline，且每次最多提交一个
缓存 response。它应在 event dispatch 和应用 completion 后运行，并与
`wl_poll_get_hint()` 合并 deadline。

ABI 18 生成 server 在每个可靠 RPC request 上自动观察非零 session；稳定状态只进行
inline equality check。首次绑定或切换时，底层 observer 会清理旧 session 的 pending/
cache、请求取消脱离 runtime 的 response，并设置 `detail.rpc.peer_changed`。应用调用
一次 `*_runtime_peer_observation_take()` 后撤销产品 lease/非 RPC 工作。建立相同产品
session 的可靠非 RPC 流量，应先显式调用 `*_runtime_peer_observe()`。

手动底层用户则自行零初始化 `wl_rpc_peer_t`，并把每个 session 与 cancel callback 传给
`wl_rpc_peer_observe()`。多 peer 产品可对离开的 peer 调用
`wl_rpc_server_discard_session()`；生成的单 peer tracker 不是 router 或 peer table。

pending timeout 与 cache TTL 相互独立且 wrap-safe，零表示禁用。
`wl_rpc_server_expired_acquire()` 返回超时 token 但不丢弃它；应用必须类型化
reject/complete 或显式 abandon。淘汰、过期、session discard 或进程重启都会终止
replay 保护，因此这不是持久化 exactly-once；非幂等产品需要持久 operation key 或
幂等 handler。
