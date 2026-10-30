# 通信身份与 C++ Session

通信 instance 通过显式 source 在初始化时获取，必须新且非零；使用平台随机源或持久分配器，失败不回退到时钟/生产固定 ID。与一个 previous 比较只是局部保护，不是唯一性证明。operation/generation 耗尽需 quiescent 后新实例。身份不是认证或业务幂等键。

可选 `wirelink::Session` 通过 C endpoint factory、同一 Executor/kernel 拥有 UDP 连接。submit 仅等有界 owner admission，成功表示请求已快照，失败不创建 call。Operation 自持到终态并拥有复制后的 typed 结果。外部 close 排除/等待提交者并 join I/O/owner，结果可跨 close 保存；owner 内 close 只请求停止，需外部 join，禁止在 owner callback 销毁/替换 Session。手动 clock 前进需 notify。测试覆盖取消、接纳失败、四并发、业务 status 和关闭竞态。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
