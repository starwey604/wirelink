# 无动态分配的 SPSC FIFO

[English](fifo.md)

`wirelink/fifo.h` 提供保持顺序和数量的应用队列，通过 `Wirelink::containers` 链接。C 持有 context 的原子存储，C++ 使用 opaque pointer。应用可用它把自持接收值交给 consumer，或把待发命令交给 owner；网络 route 与重试由应用定义。

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

## 保存类型化接收值与待发命令

在生成的普通消息回调中 claim 一个 slot，使用 `*_value_from_view` 把借用消息复制到 slot 内的自持 `*_value_t`，检查转换结果再 publish。转换失败则 abort；所有借用字段必须在回调返回前复制完成。slot 大小/对齐应按自持类型配置，不能只复制含指针的 view。参考[结果复制示例](tutorial-rpc-values-cn.md)。

队列满返回 `WL_ERR_QUEUE_FULL`，由应用选择丢弃、记录或业务流控。这个本地拒绝不会让 kernel 重试收到的普通消息。独立 codec 的 repeated view 另需每个 slot 的 backing，不能当作 bounded kernel route。

反向发送时，producer 可发布消息 ID、自持请求或有界编码 payload，并唤醒 owner。owner 暂时保留 acquire 的队首；发送接纳返回 `WL_ERR_WOULD_BLOCK` 时，等真实资源进展后重试。接纳成功或确定拒绝后 release。输入快照和等待顺序见 [kernel 模型](kernel-cn.md)。

## Reset 与统计

`wl_fifo_reset()` 丢弃未读值并把 depth 置零；活动 claim/view 时拒绝，且不得与 SPSC
操作并发。reset 保留累计计数和生命周期 high-water mark。

`wl_fifo_get_stats()` 在合法 SPSC 使用期间安全。`depth` 包含已 acquire 未 release
的队首；`high_watermark` 是 producer 观察到的最大深度。所有计数饱和于
`UINT32_MAX`，各字段只是观察快照。普通 empty/full 有专用计数，不属于生命周期错误。
