# 在 kernel 旁调度应用工作

[English](endpoint-product-policy.md)

最新值选择、有序业务、job、重试和幂等保存在显式应用状态中。独立 `Wirelink::containers` FIFO/LATEST/outbox 提供本地存储规则，由应用选择何时使用。kernel 负责类型化分发、四路调用关联和 packet 所有权。

在 owner loop 中推进有界业务工作。阻塞动作提供 readiness 或未来 deadline；将它与 kernel/adapter readiness 合并，再等待锁存唤醒。普通输出返回 `WL_ERR_WOULD_BLOCK` 后由应用保留，直到接纳成功。没有可推进状态转换时，不要反复报告立即工作。

[组合服务](../examples/04_composed_services/README.md)展示两个 port/core 上的应用公平调度和对象传输。[应用任务](tutorial-jobs-cn.md)展示短接纳、job ID 和普通完成消息。持久身份、状态查询、结果保留和关闭动作由应用定义。本地 cancel/close 不能撤回远端副作用；重试可能再次执行业务。

deadline/等待组合见 [owner loop](tutorial-integration-cn.md)。
