# Owner 执行与关闭

单 owner 向 `wl_kernel_step` 提供单调 tick 和有界 budget，deadline 先于 RX。四个 outbound call、四份 reply、一份 ordinary snapshot 共用九索引 FIFO。执行副作用前预留响应容量；BUSY 保留编码字节并等待真实 activity，独立接纳的重复请求可能再次执行。

已接纳调用只有一个终态，之后 handle 失效；接纳失败不再回调。结果仅 callback 期间借用，需保存则返回前复制。callback 可 admission/cancel，不能递归 step/close。逻辑终态不释放 USB/DMA 的物理借用。关闭 kernel、停止并 join 真实 producer、处理最终 completion，quiescent 后才释放存储；重启必须新 instance。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
