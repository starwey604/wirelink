# 无动态分配的 SPSC FIFO

> 英文版 [`fifo.md`](fifo.md) 是规范来源。

`wirelink/fifo.h` 为必须保序且保留数量的值提供应用层 `FIFO` 策略。它独立于
`wl_ctx_t`、不分配内存，也不改变 v1 frame。它既可把收到的类型化值送往应用 task，
也可把预编码命令送往 Wirelink 的单一 consumer。

## 存储与初始化

调用方提供 opaque context 和连续的固定大小 slot：

```c
struct control_event slots[8];
wl_fifo_t fifo;
wl_fifo_config_t config = {
    .value_size = sizeof(slots[0]),
    .value_alignment = _Alignof(struct control_event),
    .capacity = 8,
};
wl_fifo_requirements_t requirements;

wl_fifo_requirements(&config, &requirements);
wl_fifo_init(&fifo, &config,
             &(wl_fifo_storage_t){slots, sizeof(slots)});
```

capacity 必须非零且小于 `2^31`，从而让 32 位 cursor wrap 后的满/空判断仍无歧义。
初始化检查所有 C11 原子是否 lock-free；不支持的平台返回
`WL_ERR_NOT_SUPPORTED`。初始化和 reset 需要外部 quiesce，其他操作严格 SPSC。
多 producer 应使用独立队列或外部串行化；`volatile` 不是同步。

## 借用所有权

producer 用 `wl_fifo_write_claim()` 保留下一个 slot，填充后 publish 或 abort。
publish 通过 release 把 slot 交给 consumer，结束调用后 claim 指针失效。

consumer 用 `wl_fifo_read_acquire()` 获取最早值；该指针在匹配的
`wl_fifo_read_release()` 前只读且有效。release 后 slot 才归还 producer。每个角色只能
有一个活动 token，opaque token 会拒绝陈旧或不匹配的 finish。

全部 slot 被占用时，write claim 返回 `WL_ERR_QUEUE_FULL` 并递增
`full_rejections`，现有内容不变。FIFO 没有 drop-oldest，因为 producer 回收 slot 会
破坏 consumer 所有权。新鲜度优先时使用 `LATEST`。

## 类型化解码与命令队列

WLC route 可 claim slot 后把生成 scratch 指向 `claim.value`，从 handler 直接 publish，
避免中间消息复制。任何 dispatch 失败都必须 abort。固定字段与内联数组可自包含；
`bytes`、`string` 和 repeated 字段需要为每个 FIFO slot 单独提供有界 backing。

反向发送时，FIFO value 可做成包含 delivery、message ID、payload length 和有界编码
payload 的内联 command record。应用 producer 直接编码进 claim 并唤醒 Wirelink
consumer。若 core 返回 `WL_ERR_BUSY`/`WL_ERR_WOULD_BLOCK`，consumer 可以保留已
acquire 的队首，在 transport/protocol 推进后重试；只有接受或确定拒绝后才 release。

## Reset 与统计

`wl_fifo_reset()` 丢弃未读值并把 depth 置零；活动 claim/view 时拒绝，且不得与 SPSC
操作并发。reset 保留累计计数和生命周期 high-water mark。

`wl_fifo_get_stats()` 在合法 SPSC 使用期间安全。`depth` 包含已 acquire 未 release
的队首；`high_watermark` 是 producer 观察到的最大深度。所有计数饱和于
`UINT32_MAX`，各字段只是观察快照。普通 empty/full 有专用计数，不属于生命周期错误。
