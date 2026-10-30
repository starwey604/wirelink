# 显式 owner 时间

C kernel 从 owner 接收 now，不读取 clock。timeout 是 1..INT32_MAX tick，使用 uint32 模运算。相关工作共用单调时间快照，合并 kernel/应用 deadline 与 adapter 提示，等待前扣除 owner 工作耗时，用 latched notification 解决 query/wait 竞态。

可选 `wl_platform_environment` 提供 clock/新 instance 描述符，没有可变全局 provider。身份只在初始化获取。供 host submitter/owner 共用的自定义 clock 必须线程安全，手动 clock 前进后需显式通知 Session/waiter。tests/clock_bridge 由 C++/Python 使用 C-owned 端点，验证一次外层读取、core 零读取。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
