# 应用状态与调度

应用持有最新值选择、有序业务、job、重试与幂等状态。core 提供同步 typed dispatch 和四路调用关联，不提供 policy/service hook。独立 FIFO/LATEST/outbox 仍可通过 `Wirelink::containers` 使用，使用容器不等于建立可靠 route 或框架调度器。

应用 deadline 由 owner loop 推进，等待提示与 kernel/adapter 合并。ordinary output 返回 WOULD_BLOCK 时由应用保留到可接纳，不能在没有可推进转换时反复报告立即工作。03_device_service 和 04_composed_services 展示有界应用状态与长任务消息。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
