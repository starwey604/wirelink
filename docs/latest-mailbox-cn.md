# 无动态分配的 LATEST Mailbox

> 英文版 [`latest-mailbox.md`](latest-mailbox.md) 是规范来源。

`wirelink/latest.h` 实现应用层 `LATEST` 策略。它独立于 `wl_ctx_t`，不改变 v1
frame 或冻结的核心结构。通常由一个 producer 将 WLC 消息直接解码进 claim 的 slot，
再由一个 consumer 借用最新发布的类型化值。

## 存储与初始化

mailbox 不分配内存。应用提供 opaque context 和三个固定大小的 value slot：

```c
struct control_setpoint slots[WL_LATEST_SLOT_COUNT];
wl_latest_t mailbox;
wl_latest_config_t config = {
    .value_size = sizeof(slots[0]),
    .value_alignment = _Alignof(struct control_setpoint),
    .initial_generation = 0,
};
wl_latest_requirements_t requirements;

wl_latest_requirements(&config, &requirements);
wl_latest_init(&mailbox, &config,
               &(wl_latest_storage_t){slots, sizeof(slots)});
```

requirements 查询验证大小与对齐，并给出 slot stride、总字节数和固定 slot 数量。
storage 基地址必须满足 `value_alignment`。context 使用 C11 32 位原子；初始化会用
`atomic_is_lock_free()` 检查每个原子。不合适的平台返回
`WL_ERR_NOT_SUPPORTED`，不会偷偷引入锁。

## 所有权与生命周期

producer 调用 `wl_latest_write_claim()`，原地写完整值，然后调用
`wl_latest_write_publish()` 或 `wl_latest_write_abort()`。只有 producer 可访问
可写指针，publish/abort 后指针失效。

consumer 调用 `wl_latest_read_acquire()` 获取尚未 acquire 的最新值；无数据返回
`WL_ERR_NO_DATA`。指针在对应 `wl_latest_read_release()` 之前保持稳定。producer
可以发布和合并更新，但不能回收 consumer 正在借用的 slot。每个角色只能有一个活动
claim/view。

初始化和 reset 需要外部 quiesce。其余操作严格为 SPSC；多 producer/consumer 即使
调用时间没有重叠，也不在 C memory model 约定内。

## 直接解码生成消息

WLC `LATEST` route 无需中间 decoded-message copy：wrapper 先 claim，把生成 route
的 scratch 指向 `claim.value`，handler 成功后直接 publish。dispatcher 已释放 RX
事件；codec、missing-route 或 handler 失败时，wrapper 对仍活动的 claim abort 一次。

固定字段和内联固定数组可直接解码。保留类型必须自包含；借用 RX event 的
`bytes`/`string` 指针不合格，除非 route 在 `wl_event_release()` 前把内容复制进有界的
slot 自有存储。完整模式见 `tests/zephyr/integration/application_runtime`。

## 为什么普通值不会发生数据竞争

三个 slot 分别具有 producer 独占的 `back`、原子的 `middle` 和 consumer 独占的
`front`。producer 通过 release exchange 把 `back` 放入 `middle`；consumer 通过
acquire exchange 把 dirty middle 移到 `front`。所有权在任何一方复用 slot 前完成
传递，因此 payload 可以是任意非原子 C 结构；这不是 seqlock，reader 不会在 writer
修改同一内存时复制它。

## 新鲜度与统计

publish 覆盖尚未读取的 dirty middle 时递增 `coalesced`；consumer 已 acquire 的值
不计入。成功 publish 的 generation 按 32 位模加一，允许 wrap 后为零。
`initial_generation` 可在重启后延续 generation。

`publishes`、`reads`、`coalesced`、`empty_reads`、`resets`、`errors` 饱和于
`UINT32_MAX`。统计是各字段独立加载的观察快照，不是单个原子事务。重复 claim/acquire
返回 `WL_ERR_BUSY`；陈旧或不匹配的 token 返回 `WL_ERR_INVALID_STATE`。
`wl_latest_reset()` 丢弃未读值但保留 generation 和累计计数；有活动 token 时失败。
