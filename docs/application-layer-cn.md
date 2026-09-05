# Wirelink 应用层约定

状态：类型化 routing、`LATEST`、`FIFO`、RPC、顺序 bulk 和可选 C++20 host executor
均已有实现；embedded task/queue 集成仍由平台负责。英文版
[`application-layer.md`](application-layer.md) 是规范来源。

本文划定冻结的 v1 link protocol 与其上的无动态分配应用设施之间的边界。typed
dispatch、delivery policy、RPC correlation 和 bulk 都属于 payload layer，不得向 compact
v1 header 增加字段。

## 1. 分层与非目标

1. platform adapter 移动完整 transmission unit；
2. Wirelink core 对 DATA 成帧、链路重试/去重，并暴露借用 `wl_event_t`；
3. WLC bindings 解码和路由应用 payload；
4. 可选 runtime 提供 `LATEST`、`FIFO`、RPC 和顺序 bulk 状态。

可靠 DATA 的 ACK 只证明合法 packet 到达 peer 的稳定 event storage。
`WL_EVT_TX_SUCCESS` 对可靠 DATA 表示 link delivery，对不可靠 DATA 只表示本地发送成功；
都不表示 command 执行成功。只有声明的 response/status 解码后，应用 operation 才完成。

应用层不提供持久化 exactly-once、认证、peer discovery、多 peer routing，也不透明共享一个
context 给多个 transport。这些属于产品策略。

## 2. Context 与执行所有权

一个 `wl_ctx_t` 表示一个 link profile 和一个 logical peer。每个 physical link/peer 拥有
独立 context、RX/TX storage、session ID、router 和应用状态。跨两条 link 镜像消息必须由
应用显式 fan-out；两个 adapter 的 RX 不得输入同一 context。

- 一个 producer 独占 RX feed/reserve/DMA/unit queue；
- 一个 consumer 独占 adapter service、send、`wl_poll()`、transaction query 和
  `wl_event_release()`；
- callback/ISR 只发布字节或 completion 并 wake consumer，不运行 decoder/handler。

线程化 runtime 使用专用 consumer task/executor，其他线程经有界 command queue 进入。
`Wirelink::host` 实现 owner thread、SPSC RX、wake semaphore 和 fixed coalescing outbox；
embedded 产品提供对应 RTOS task/queue。C core 不加隐藏锁。

consumer 顺序为：消费 adapter completion；poll/route 立即 event；推进 RPC/transfer
deadline；提交有界 queued command；等待 adapter/command 活动或最早 deadline。

## 3. 类型化 Dispatch 与借用生命周期

生成 dispatch 将永久 `message_id` 映射到一个 decoder/handler descriptor。unknown ID、
codec failure 和 handler failure 是独立结果和计数。默认流程：

```text
wl_poll -> decode borrowed payload -> invoke typed handler -> release event
```

dispatcher 在 handler 返回后恰好 release 一次；handler 不 release。借用的 bytes/string
及 payload 内指针随即失效，必须在返回前复制/移动进调用方存储。handler 不得递归调用同一
router/context。

| Policy | 行为 | 用途 |
| --- | --- | --- |
| `DIRECT` | 在 consumer context 直接 callback，不保留值 | 立即消费的 command |
| `LATEST` | 直接解码进三 slot SPSC mailbox，新值合并未读旧值 | freshness 优先的 setpoint/telemetry |
| `FIFO` | 直接解码进有界 SPSC ring，满时拒绝新值 | 顺序与数量必须保留的 event |
| `RPC` | 把 operation metadata 路由到固定 client/server slot | request/response/application status |

`LATEST` 的合并发生在 decode 后，不是 core RX 行为；lock-free C11 front/middle/back 交换
使普通结构不会 tearing，原子不 lock-free 时返回 `WL_ERR_NOT_SUPPORTED`。`FIFO` 借用
最老值到 release，producer 不可覆盖 unread/borrowed 数据；满时拒绝而不 drop-oldest。
variable-length borrowed field 若要保留，必须提供明确 backing 和有界 copy policy。

## 4. RPC 与应用完成

RPC 由成对 WLC message 和 runtime config 表达，不进 link header。每个 RPC 带非零
`uint32` operation ID；零保留给非关联消息。ID 在单 peer context 的 retained slot 中唯一，
peer 仍可能保留 response cache 时不得复用。

service binding 固定 request/response ID、各自 delivery、response status/payload、local
deadline/cancel policy 和 handler 模式。link 与应用状态分离：

```text
FREE -> QUEUED -> LINK_PENDING -> WAIT_RESPONSE -> COMPLETED
                    |                 |               |
                    +-> LINK_FAILED   +-> TIMED_OUT   +-> APPLICATION_ERROR
                                      +-> CANCELLED
```

link success 只把可靠请求推进到 `WAIT_RESPONSE`。精确 response 可在 `LINK_PENDING` 提前
到达并完成 RPC，但独立 core handle 仍要 cancel/drain/take。server `begin()` 在 handler
之前把请求分类为 `NEW`、`PENDING_DUPLICATE`、`REPLAY`、`CONFLICT`；`NEW` 先保留
pending metadata 和 response slot。应用可同步完成、保存 generation token 异步完成，或
abandon。cancel 是 best effort，无法撤回 peer 已收到的请求。

完成的 cache entry 可 replay 而不重执行；进行中的 duplicate 被抑制；同 session 内 ID
冲突被拒绝，新 session 的相同 ID 是独立请求。cache eviction/restart 后不保证 exactly-once；
关键非幂等操作需要持久 operation key 或幂等 handler。

ABI 18 会在可靠 RPC handler 前自动观察 peer session；切换清除旧 pending/cache 和
detached response，`*_peer_observation_take()` 让产品撤销 lease。建立同一权限边界的可靠
非 RPC 消息需显式 `*_runtime_peer_observe()`。多 peer 仍需每 peer context/runtime 或
产品 peer table。

## 5. 无动态分配存储模型

初始化必须计算/验证 core RX/TX/event、decode scratch、LATEST slot、FIFO capacity、RPC
slot/cache、cross-thread command entry、bulk state 与 source/sink 的全部长生命周期存储。
超容量不得偷偷分配，而应返回类型化错误并计数。可靠 link ACK 不等于应用接受；router
必须返回业务 busy/error 或执行文档化 retry policy。

应用设施使用独立 config/storage 结构和 requirements query，不向冻结的
`wl_config_t`、`wl_storage_t`、`wl_event_t` 追加字段。生成 assembly API 提供机械默认值、
role helper、精确 requirement，在 payload 全部有界时提供默认 aligned arena。
`*_runtime_init_checked()` 用于 bring-up 诊断，证明配置后使用较小的普通 init。

## 6. Error 与 Health Domain

session-facing 错误必须保留来源：Transport、Link、Codec、Routing、RPC、Application。
不能全部压成 timeout 或 generic I/O。health snapshot 应组合 last-valid-RX、transport
连接、core counter delta、route drop、RPC outcome 和应用错误；普通 unreliable loss 不
等于 disconnect。可选 diagnostics target 只格式化已有 snapshot，不增加 registry、logger、
clock、allocation 或自有 counter。

## 7. Multiplexing 与 Transport

`COBS_STREAM` 只靠 `0x00` delimiter 重同步，首字节是 COBS code，不是稳定 protocol
marker。不能把同一 byte stream 同时喂给多个 stateful parser 直到某个接受。共享物理
stream 必须使用独立 endpoint/channel、所有 parser quiescent 时的带外 mode switch，或
在 Wirelink unit 外包带明确 channel ID/length 的 multiplexer。外层负责跨 channel 重同步。

## 8. 顺序 Bulk 扩展

大于 `WL_FRAME_MAX_PAYLOAD` 或不能整对象驻留 RAM 的数据使用独立应用状态机，不扩大
core 常量或修改 compact v1。首版是 single-peer、single active、upload、strict sequential：

```text
Begin -> Chunk x N -> End
           ^          |
           +-- Status-+
Abort -------- Status
```

五个永久 message ID 分别表示 Begin、Chunk、End、Abort、Status。sender acquire action，
本地接受后 submitted，背压时 defer。receiver 同步调用 sink，然后 acquire retained Status；
Status 只有本地 TX 接受后 release。双方 poll wrap-safe timeout，deadline hint 参与 owner wait。

| Message | 必需语义字段 |
| --- | --- |
| `Begin` | 非零 `transfer_id`、`total_length`、请求 chunk size、对象 CRC32C |
| `Chunk` | `transfer_id`、绝对 byte `offset`、借用 `bytes` |
| `End` | `transfer_id`、重复的 total length 与对象 CRC32C |
| `Abort` | `transfer_id`、应用 reason |
| `Status` | `transfer_id`、已确认 phase、result、累计 `next_offset`、接受的 chunk size |

`Status.next_offset` 是唯一应用级累计确认。link ACK 早于 decode 和 durable sink，不能当作
存储成功。receiver 只在同步 sink 操作返回后发 Status。重复 Begin/已消费 Chunk/终态 End
不得重复副作用。receiver 协商 chunk 上限和 resume offset，只接受精确 next chunk；完全在
offset 前的 duplicate 只确认不重写，gap、partial overlap、overflow、越界被拒绝。

Chunk bytes 借用 event，只能在同步 `write(offset, span)` 内消费；`BUSY` 必须表示零消费、
零 durable 变化。runtime 不保留 span、不分配对象 buffer，只保留常量大小状态。End 对实际
存储内容执行一次最终 verify/commit；CRC32C 是互操作 baseline，产品可追加 digest/signature。

Abort tombstone 可先于延迟 Begin；相同 ID 后续拒绝。替换失败 receiver 前先 abort 旧 sink。
sender 每次只有一个应用消息 outstanding，Status timeout 后有界重试；BUSY 使用非零 delay。
USB/UDP 通常对 Chunk/Status 使用 unreliable，避免两层 stop-and-wait。reset、force-abandon、
idle timeout 和 transfer ID 复用必须遵守英文规范。首版不包含 window、乱序重组、对象 RAM
staging、异步 Chunk borrow、download 或跨重启 resume。

## 9. 必需验证

测试必须覆盖生成向量/兼容性、borrow release、LATEST wrap/coalescing、FIFO full、link ACK
后应用拒绝、RPC loss/duplicate/replay/conflict/timeout/cancel/async、queue race、多 peer 独立
状态、multiplexer 恢复、bulk interruption/duplicate/resume/integrity/RAM bound，以及 host、
native_sim、QEMU、sanitizer、fuzz、stack 和代表硬件性能。link conformance vector 始终逐字节
不变。
