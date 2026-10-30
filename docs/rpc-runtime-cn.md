# 有界同步 handler RPC

RPC 只有共享四槽 call table 和 typed 同步 handler。接纳时快照请求，handler 在 owner callback 中填响应并返回有符号业务状态，副作用前预留 reply。BUSY 保留编码响应而不重跑 handler，不保留未来去重历史。

没有 replay/TTL、deferred token 或 inspect/release。callback 结果用公开 value conversion 保存。本地超时/取消不能撤销远端，重试可能再次执行。长任务先返回应用 job ID，再用应用状态消息/查询恢复。生成 C、C++ Session、SDK 共用同一 core，不复制关联表。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
