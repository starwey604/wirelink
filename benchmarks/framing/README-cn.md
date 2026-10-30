# 独立帧工具 benchmark

[English](README.md)。

当前 `wirelink-framing-v3` 只测公开 frame 工具的宽余容量、精确容量、重叠输入编码、
raw 解码及 COBS 解码，不含端点或执行引擎。三种 envelope／integrity selector 属于
独立工具；默认内核不经过它们。冻结 reliable DATA／ACK 编码向量保留，不代表端点
仍支持可靠发送。

旧 send／claim／busy3／retry3／engine receive 工作负载随旧引擎移除。
所有权、BUSY、停止和物理完成合同由 packet kernel／adapter 测试验证；当前
RPC／Sample 性能由独立静态内核 USB benchmark 测量。报告解析器保留 v1/v2 历史
格式，但拒绝与 v3 跨工作负载比较。`context_bytes=0` 明确表示没有引擎，
`fixture_bytes` 仅为本工具暂存区大小。

构建、过滤和采集命令见英文页。Google Benchmark 依赖必须显式提供，CMake 不下载。
记录源码、二进制 hash、编译器、flags 和机器，改源码前冻结二进制并立即 stamp 原始
报告。比较器校验源码 hash、暂存布局、完整 repetition 和测量集合。

Zephyr 版本复用纯 C workload，输出完整 v3 begin/end 记录。每批 32 次时屏蔽中断，
只说明工具固有成本，不能当作应用响应、端到端 CPU 或 USB 吞吐数据。
独立按位 CRC／帧头、容量／别名向量继续位于
`tests/zephyr/unit/framing_workload`；不需刷板即可运行。
