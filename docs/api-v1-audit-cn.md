# Wirelink v1 C API 历史审计

状态：1.0 前收敛期间，本文已被 [`api-boundary-cn.md`](api-boundary-cn.md) 取代，不再是
ABI freeze；仅保留为 0.9 审阅和 release check 的历史记录。Compact-v1 transmission
unit 仍由 `conformance-v1.md` 冻结，但 1.0 前 public C declaration 仍可修改。

## 当时保留的 ABI 边界

历史方案把 `wl_ctx_t` 固定为 896-byte、`max_align_t` 对齐的 opaque object；1.x 内部可
使用更多 reserved storage，但 public size/alignment 不变。public enum-like type 使用
`int32_t`，不受 `-fshort-enums` 影响；含 pointer/`size_t` 的结构仍依赖架构。

当时计划关闭的 v1 struct 包括 span/codec view、frame/wire view、`wl_config_t`、
storage/requirements、event/TX result、poll hint，以及 RX counter/claim/queue 和 TX payload
claim。新能力应新增函数与独立类型，而不是追加字段；多 TX slot、sliding window、profile
negotiation、fragmentation 需要独立的 post-v1 API 设计。

## 生命周期决策

`wl_init()` 是 context 首次操作。重初始化前 adapter 必须停止并归还全部 RX/TX borrow。
零初始化但未 init 的 context API 返回 `WL_ERR_NOT_INITIALIZED`。之后由一个 consumer
独占 send/poll/release/query/TX completion/adapter service，一个 producer 独占所选 RX
lifecycle，核心不加锁。

sink rebinding 需要 quiesce；更换 callback 不会取消已借用 TX unit。COBS consumer 可用
`wl_feed_recover_reset()`，native packet/length-prefixed 返回 not supported。
`wl_poll()` 在 no-data 前清空 output event，避免复用旧 payload pointer。session ID 由应用
提供；sequence 耗尽时停止 adapter、选择新非零 session 并重新初始化，不静默复用。

## 1.0 前已经实现的加法 API

borrowed TX claim/commit/abort 允许 WLC 直接编码进 retained payload。native profile 使用
最终 TX unit，stream/length profile 使用核心 retained region。claim 由 consumer 拥有且
最多一个；普通 send 在 claim 活动时拒绝。可靠重传仍使用稳定 encoded unit。

`wl_poll_get_hint()` 无副作用地填充固定宽度 `wl_poll_hint_t`。`work_pending` 是 0/1；
`next_deadline_ms` 是相对 delay，`UINT32_MAX` 表示无 deadline。已有 event、完整 COBS、
overflow、committed unit、到期 ACK/retry 都是 immediate work；leased RX 会阻挡后续 RX，
但不隐藏 TX event/deadline。sink busy 或 in-flight 后的 queued work 不报告 immediate，等待
外部 writable/completion wake 后再 poll 一次，避免忙循环。

## 当时的 Release Gate

installed package 需通过 strict C11、strict C++20、`-fshort-enums`；GCC/Clang 对 frame/
protocol hot path 保持 256-byte fixed-frame ceiling；ESP32-S3 USB 普通和 CPU telemetry build
进入 CI。1.0 前必须保持 compact-v1 vector、不破坏 package consumer、覆盖 borrowed TX/
scheduling ABI、通过 unit/sim/QEMU/sanitizer/fuzz/adapter 测试并守住 MCU stack gate。

这些是历史依据；当前仍待决定的 API 问题以中文
[`api-boundary-cn.md`](api-boundary-cn.md) 末尾为准。
