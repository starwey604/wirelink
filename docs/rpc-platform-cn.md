# Host owner 与等待

`wirelink/host/executor.hpp` 绑定一个 C kernel 和可选 I/O callback；start 成功前所有权仍归调用方。队列 32 槽，每最多八条命令后推进 core；执行或关闭拒绝均在 owner。Executor 没有 RPC 表、应用调度器或 payload pool。

owner callback 可 request_stop，不能 join/销毁自身。外部 stop 关闭逻辑工作、停止真实 producer、处理最终 completion 并 join 后才释放存储。notify 必须非阻塞、latched、线程安全且不重入业务。启用 WIRELINK_BUILD_HOST_RUNTIME；测试见 runtime/host/tests 与 benchmarks/owner_pass。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
