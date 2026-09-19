# Executor owner-pass 记账

[English](README.md)。这是一个确定性的调度回归，不是 CPU 基准。
不需要 WLC、Asio、Google Benchmark 或硬件：

```sh
cmake -S . -B build/owner-pass -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_HOST_RUNTIME=ON -DWIRELINK_HOST_ACTIVITY=ON \
  -DWIRELINK_BUILD_OWNER_PASS_TESTS=ON
cmake --build build/owner-pass --parallel 2
ctest --test-dir build/owner-pass --output-on-failure
build/owner-pass/benchmarks/owner_pass/wirelink_owner_pass
```

harness 把第一次 owner pass 门控为发布一批已就绪的工作。这个测试专用门控**不是** executor 的计时批处理策略。
测试校验 payload、交付计数、有界 dispatch、RX/RPC 公平性、应用请求的 follow-up、异步 TX、
背压 sleep/resume、空 feed、溢出恢复，以及持续补充时的停止。
一次 proxy 完成走真实 caller/owner 交接；生成的 RPC/UDP 集成另行测试。兼容性用例有意省略 adapter readiness hint。

每个场景在 join owner 后输出一行 JSON。单独的溢出检查使用断言。启动/关闭通知也计入。
线程启动可能与第一次 pass 竞争；不要把精确 idle-pass 计数当作可移植门槛。

`WIRELINK_HOST_ACTIVITY` 默认 OFF。它启用有界 relaxed 原子计数器，不是时间戳或热路径日志。
OFF 时 `Executor::activity()` 返回 disabled/zero；诊断绝不能控制应用同步。
PUBLIC 定义随 `Wirelink::host` 导出；一起重建所有消费者。

重要区别：

- `passes` 是外层 executor 迭代，不是 endpoint step 或 OS 上下文切换。
- `no_reported_work` 表示没有 pump 报告的进展、LATEST 进展或收集到的 RPC job。
  它可以包含一次必要的 idle/readiness 检查或未报告的 adapter 工作；本身**不能**证明 CPU 被浪费。
- `rpc_batches` 计非空收集；`rpc_empty_collections` 计没有产出的陈旧 pending hint。
  `rpc_completions` 也包含失败/取消的结果。
- `latest_attempts` 包含 deferred/rejected 尝试。`latest_sent` 指核心接受，对异步 adapter 先于物理交付。
- `notifications` 计调用次数，不是信号量释放或真实线程切换。`waits` 计等待尝试，可能消费一个已存在的通知。
- `continue_*` 标识请求下一次 pass 的分支；现实中原因不必互斥。
  执行中快照是 relaxed 的，不是一致的多计数器事务；请比较 join 后的最终快照。

CPU 和「提交到 sink」年龄用另一个
[executor 基准](../api/EXECUTOR-cn.md)，并关闭 activity/timing 插桩。
