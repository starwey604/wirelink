# 发布前 benchmark 清单

[English](RELEASE.md).

选择产品合同与工作负载兼容的两个已提交版本。0.13 裁剪前后的发布版不能直接用当前 preset 重建混比，应使用封存工具并明确语义变化。运行 benchmark 不授权 push 或发布。

1. 先完成正确性、包和编译器检查。设置匹配的本地 `WIRELINK_WLC_EXECUTABLE`（0.13）和 `WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR`（1.9+）。
2. 执行 `python3 benchmarks/bench.py doctor --preset full`。使用同一台已标识的空闲主机，固定编译器、flags、电源策略和 affinity；构建/测试并行时的计时不能作为合格性能证据。
3. 选择兼容的 base/candidate，保留完整输出目录。七个套件分别由 smoke、quick、api 覆盖一次：

```sh
python3 benchmarks/bench.py pair --preset smoke --base BASE --candidate CANDIDATE --out out/release-smoke
python3 benchmarks/bench.py pair --preset quick --base BASE --candidate CANDIDATE --out out/release-quick
python3 benchmarks/bench.py pair --suite api --preset full --base BASE --candidate CANDIDATE --out out/release-api
```

4. 核对 raw、manifest、normalized、comparison、Markdown 与 JUnit。退出 0 表示有效且启用门槛通过，1 表示有效回退，2 表示无效/不可比较。smoke 失败或退出 2 必须调查；主机计时在固定 testbed 校准前保持诊断性质。模型是模拟时间/计数；footprint objects 不是链接后产品 RAM；API pair 包含真实 RAM 传递与 owner 工作量。
5. 影响物理路径时执行对应固件/HIL 流程，保存源码、配置、二进制、observer 设置、原始采集、失败尝试及最终板卡状态。硬件数据与主机微基准分开；工作负载变更后重新判断可比性。

普通 push/PR 不运行性能测量。手动 benchmark workflow 使用当前 preset；远程 CI、硬件实测和版本发布均需各自真实执行记录，不能推定通过。
