# 无动态分配的 RPC Runtime

[Readiness 与到期 expiry](execution-contract-cn.md)

> 英文版 [`rpc-runtime.md`](rpc-runtime.md) 是规范来源。

`wirelink/rpc.h` 在不修改 Wirelink v1 帧头的前提下关联请求与响应。新应用优先使用
[RPC 教程](tutorial-rpc-cn.md)中的生成端点；本文先说明托管 RPC 的通信与所有权边界，
后半部分是高级底层接口。

## 托管 RPC 与已有协议映射

RPC 请求和响应分别默认可靠；可在对应绑定后写 `@delivery(unreliable)` 覆盖。
显式 `@delivery(reliable)`、省略默认、旧 `request_delivery = reliable` 等价，
生成代码与 profile identity 相同；同一方向重复配置会报错。这不改变 LATEST/FIFO 的显式策略。

在 `.bind.wl` 中同时省略 `request_operation_id`、`response_operation_id`、
`response_status`，即选择托管 RPC。`.wl` 只定义业务参数；runtime 自动分配调用编号、
检查响应类型并补齐回复元数据。业务 codec 和 schema identity 不依赖这些运行策略。

三个映射全部写出，则选择已有的映射模式，保留原有 payload 编码和 profile identity。
请求、响应的字段名称可以不同，同一次调用携带的编号值必须相同。只写一部分映射会编译失败，
接收端不会自动探测或回退到另一种格式。

托管 RPC 使用尚未发布的 `managed-rpc-v4-tagged-varuint32` 格式：

| 偏移 | 字节数 | 内容 |
| --- | --- | --- |
| 0 | 8 | 非零原客户端 session，无符号大端；每个响应完整回送 |
| 8 | 1–5 | `(operation_id << 1) | rejection` 的规范无符号 LEB128；保留完整非零 32 位操作编号 |

native message ID 已确定请求／响应。关联值最低位仅标识拒绝：请求和成功为零，
拒绝为一。正常元数据为 9–13 字节；拒绝随后追加非零有符号 BE32 状态，不带业务体，
合计 13–17 字节。请求携带拒绝位、零编号／session、非规范或溢出的关联值、
拒绝状态／长度错误均报 `WL_RPC_ERR_MALFORMED_METADATA`。元数据不提供认证；
可靠请求身份须与链路头一致，响应身份不匹配本地客户端时报告
`WL_RPC_ERR_SESSION_MISMATCH`，不改变其他调用。

私有 RPC 业务体保留字段 tag，将 scalar `fixed32` 的值以完整 uint32 域的 varint
编码，wire type 为零；嵌套消息和 repeated scalar 采用同一规则。packed fixed32
数组保持原固定字宽。其他类型、字段编号、presence、必填检查及未知字段跳过语义
不变。runtime 自动生成自己的私有 codec，公开 `*_encode()`／`*_decode()` 仍使用
冻结的 Schema-v1 TLV 字节；业务类型不变，也不限制整数取值范围。去重指纹仍基于
原公共 codec 的业务规范编码，不根据新的传输编码计算。

本轮变更尚未发布；两端一起重新生成和重编译，无旧格式兼容或协商分支。
新格式进入 binding-profile identity，并在 runtime manifest 中独立记录。
mapped RPC、公共 codec 及 Compact-v1 基础帧不变。varint 节省多少字节取决于值，
生成存储上界覆盖完整整数域，不能把 benchmark 的常见长度当成所有输入的上界。

## 普通异步调用的所有权

普通入口是 `<runtime>_endpoint.h` 和 `endpoint_<service>_async()`：请求为自持值，
接受前快照；完成时先准备自持结果、回收调用，再通知 callback。失败提交无回调，
已接受调用在持续推进或有序 close 下恰好通知一次。超时必须为 1..2³¹−1 毫秒且包含排队。
取消使用可选 `wl_rpc_call_t`；常规路径没有 inspect/release。
`config.on_<service>` 的即时 handler 返回零表示成功、非零表示业务拒绝。
普通结果的指针只在回调内有效，复制结构体即可长期保存。

默认托管端点为四槽最近结果策略；`config.advanced` 保留专家覆盖，严格缓存选 REJECT_NEW。
同步调用见[平台等待](rpc-platform-cn.md)，可选 create/destroy 见[端点存储](endpoint-storage-cn.md)。
下文手动调用和 deferred token 是明确的高级选择，
不能把高级 handler 的非零返回规则套用到即时 handler。

## 高级手动调用与延迟回复的所有权

使用这些端点助手前，显式包含 `<runtime>_advanced.h`。

默认端点初始化时配置一次 `wl_clock_t`。call/complete/reject 内部取时间；
step 中的立即回复复用本轮时间，首次或空闲后调用都无需先 step。
高级 runtime 保留显式时间，详见[时钟边界](endpoint-clock-cn.md)。

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
用 `*_reject(..., 非零状态)` 回复业务拒绝。handler 返回非零表示本地放弃，
不会自动发送业务拒绝。请求里的借用字段只在回调期间有效。客户端响应必须有静态上界，
RX 将成功业务体解码一次到该调用的 owned union。高级 inspect 返回 `const *_value_t *`，
有效至 release/close；赋值复制 `*response` 即可独立保存。async prepare 先复制到独立
completion union，再回收调用容量，最后通知；sync 复制此 owned completion。以上读取
路径均不再次解码。

并发响应按调用匹配，不要求按发起顺序到达。未知／已释放调用的回复、已到终态调用的回复，
托管 runtime 会忽略并返回 `RUNTIME_OK`；可选 `on_result` 仍能从 `detail.rpc.rpc_result`
读到 `NOT_FOUND` 或 `INVALID_STATE`，但不会让另一条调用失败。畸形元数据、错误响应类型和
codec 错误仍作为明确的分发错误报告。

默认托管端点容量可在编译时配置，默认四槽；高级 runtime 可独立提供存储。首次由编号捕获句柄需要扫描，
后续底层句柄查询／取消／释放为 O(1)。底层句柄仅在单次 client 初始化生命周期内有效。
代次状态仍装在原有的 64 字节 client／slot 存储中，不引入全局计数器、堆、线程或时钟。
高级手动托管 runtime 的请求直接写入链路 TX claim；普通异步路径先编码到有界请求队列，再复制到 TX claim。响应直接写入
预留的缓存区。链路与响应容量包含最大 13 字节元数据和私有 RPC 业务体上界，
空响应也至少保留 17 字节容纳拒绝；公共 codec 上界不变。请求指纹只覆盖原公共
业务规范编码，在本地计算，不作为字段发送。

## 调用关联不等于业务幂等

每次托管 `call()` 都是新操作，没有让普通用户指定内部编号来重试的入口。
服务端会在配置的会话／缓存／过期范围内抑制重复通信请求并重放响应，但相同参数发起的新调用
可以再次执行。需要跨重试或持久化幂等时，应显式设计业务键和状态机。

托管 RPC 使用原客户端 session 与编号共同匹配响应；端点重建自动生成新身份，旧实例的
同号响应不会完成新调用。不可靠请求也携带身份并划分缓存。身份不是认证或有序版本，
不保证任意旧请求不会再次执行。显式映射模式没有新增该字段，仍须由集成保证旧响应
隔离。来源与耗尽规则见[自动会话](session-cn.md)。

## 底层分配与调度

runtime 无 heap、无隐藏锁。client slot、server pending/cache slot 和有界 response
字节存储均由调用方提供。所有调用属于拥有 `wl_ctx_t` 的同一 consumer；生成 handler
可把 response 路由到该 consumer，但不得递归调用 context。

## 底层 Client 生命周期

`wl_rpc_client_begin()` 保留 slot 并启动端到端 deadline，覆盖 `QUEUED`、
`LINK_PENDING` 和 `WAIT_RESPONSE`。零表示不超时；非零值必须小于 `2^31` ms，保证
wrap-safe。

client/server 的 `*_get_deadline_hint()` 无副作用，返回最近的相对 deadline：到期为
`0`，不存在为 `UINT32_MAX`。consumer 可与 `wl_link_get_readiness()` 取最小值；只有
`poll()` 推进 RPC deadline。这些查询只报告计时任务。生成的
`*_runtime_readiness()` 还会根据 delivery 资源，把待发送 server response 报为
runnable 或 blocked；休眠前应使用这份完整 readiness。

可靠 request 编码发送后，用 `wl_rpc_client_bind_tx()` 绑定 `wl_tx_handle_t`，并把终态
TX event 交给 `wl_rpc_client_on_tx_event()`。`WL_EVT_TX_SUCCESS` 只进入
`WAIT_RESPONSE` 并设置 `link_delivery_confirmed=1`，不会完成 RPC。底层 API 调用者还要
`wl_tx_take()`；WLC runtime 匹配事件后会同时完成两步，此时不要重复 take。

不可靠 request 或无可关联 handle 的 wrapper，在本地提交成功后调用
`wl_rpc_client_tx_completed()`，以 `link_delivery_confirmed=0` 进入等待。处于
`LINK_PENDING` 时收到精确 response 也可完成 RPC，因为应用响应比 ACK/TX 事件顺序更
强；保留的 TX handle 仍需独立 cancel 或 drain/take。`QUEUED` 状态尚未发送时收到的
response 会被拒绝。

`wl_rpc_client_response_prepare()` 先检查 request/response message、operation、
活跃状态与截止时间，返回带 generation 的 authority 和结果槽索引。生成器向对应的
owned 成员解码，成功才调用 `wl_rpc_client_response_commit()`；commit 再校验代次、
状态和截止时间。解码失败保持 pending，不发布部分值。status 为零发布 `COMPLETED`
和 `response_valid`，非零发布 `APPLICATION_ERROR` 且无成功值。
core client config 只含 slots/count/next ID，不接收业务指针。

生成 storage descriptor 除 byte arena 外，另含实际 `*_rpc_result_value_t` 数组
`rpc_results` 及 `rpc_result_count`。requirements 单独报告所需数量；init 在修改
instance 前检查数量、对齐、乘法溢出及与 arena/instance 重叠。默认 descriptor 和
endpoint 自动提供这些对象。合同 0.10 已删除客户端 encoded response 池、
`client_decode()` 和 `decode_owned_response()`。mapped RPC 的关联字段在业务体内，
因此一次解码到 owned scratch 后核对调用，再复制到对应槽。服务器 pending/replay
的稳定 encoded 存储继续保留。

cancel 只停止本地等待。先 inspect 取出绑定 handle，再按需 `wl_tx_cancel()`；晚到
completion 会被拒绝。timeout 同样不会释放 core transaction，最终仍必须在终态后
`wl_tx_take()`。终态 slot 和 operation ID 在显式 release 前一直保留。

自动 ID 单调递增并跳过本地仍保留的 slot。映射模式的生成 client start 会使用 request 中已有的
非零 ID，否则自动分配。release 后，用相同 ID 和 canonical request 重试可命中 server
的有界 replay cache；相同 ID 搭配不同数据会在 cache 存续期间冲突。

## 按方法选择服务器保证

省略 `guarantee` 或声明 `guarantee = replay;` 保留下节所述去重与有限结果重放。
显式 `guarantee = correlate;` 允许每个合法到达独立 admission/执行，包括相同
session/operation 的重复请求；它不比较 canonical request，也不提供结果历史。
两者都不承诺持久 exactly-once，取消客户端调用不撤销远端副作用。

所有方法共用 `rpc_server_pending_slot_count`；replay 的 history 数量/编码容量使用
`rpc_server_cache_slot_count` / `rpc_server_response_capacity`，correlate 的待发池
使用 `rpc_server_correlate_slot_count` / `rpc_server_correlate_response_capacity`。
只生成实际策略的字段；混合服务的两组编码上界分别计算。默认配置覆盖完整方法
上界；高级调用者显式设置的编码容量仍是 handler 可提交响应的硬上限。

correlate 在 handler 前同时预留 pending 和稳定响应空间，每次执行有独立 generation。
完成清掉 pending 后，响应仍占用待发槽；defer/背压不释放，可靠 TX 要等 terminal，
不可靠投递要等 sent 后才释放。旧 token、旧响应或别的 execution 不能回收新执行。
全 correlate 不生成 fingerprint/history/TTL 路径；这类性能必须单独标注保证差异。

## Server 去重与 Replay（默认策略）

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
`wl_link_get_readiness()` 合并 deadline。

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

## 小指令缓存下的构建与测量

ESP32-S3 cross benchmark 的本轮优化在默认 16 KiB ICache、240 MHz、
共同 CRC16／COBS、ARQ-off、单 inflight 下测量。采用统一 `-Os` 和应用层
LTO，生成 RPC dispatcher 与 core 帧解析保持独立边界；全局禁止内联的实验
并没有改善延迟。相同编译档也用于 eRPC／CSP 对照，不能与旧 `-O2` 榜单混排。
这些设置不扩大 ICache，也不把 RPC 热路径移入 IRAM。

每条 RPC 在 API 计时边界外采集 UART TX+RX 字节，按该条真实占线时间扣除
后才计算剩余耗时百分位。该指标包含关键路径的软件、调度和缓存等待成本，
并非纯 CPU 周期。结果只适用于对应的 schema、payload 和 UART 负载，不能
当作所有应用的硬实时保证。两端需使用同一新生成 RPC wire 格式。
