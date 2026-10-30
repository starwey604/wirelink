# 内核 owner 命令／停止模型

[English](README.md)。开启 `WIRELINK_BUILD_OWNER_PASS_TESTS=ON` 即可构建，不需要
WLC、Asio、Google Benchmark 或硬件。此模型检查调度，不测 CPU。

首条测试命令通过显式 barrier 暂停 owner，调用方填满32个队列槽并验证第33次接纳
失败，再通过真实 C-owned packet port 发布一个 RX unit。释放 barrier 后，八条命令
预算让内核获得一次推进：barrier 已占一条，前七个排队命令尚未见到 RX handler，
第八个命令必能观察到处理结果。没有 wall-time 性能门槛。

第二组在 barrier 阻塞期间请求 stop，验证32条排队命令全部由 owner 拒绝、不执行
业务 work、I/O stop 恰好一次，并在 join 后验证内核 quiescent。

C 持有 kernel／port／atomic 存储；C++ 只提供真实 executor 和测试外部计数，不恢复
通用 service、RPC 表或 activity-counter API。JSON 格式为 `wirelink-owner-kernel-v1`。
启动 service 次数有调度差异，只作诊断；队列界限、交付和 RX 进展是精确合同。
旧 `WIRELINK_HOST_ACTIVITY` 明确报错。物理 I/O 合同和 RPC／Session 在各自套件
验证，这个命令队列模型不提供端到端 RPC 时延结论。
