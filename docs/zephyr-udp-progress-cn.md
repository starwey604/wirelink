# Zephyr UDP：M2/M3 实施与验收记录

## 范围

M0 为 socket 合同，M1 为默认端点装配。本轮继续完成：

- M2：真实 Zephyr UDP 转发路径上的丢包、重复包与恢复测试；默认关闭的开销插桩。
- M3：独立 client/server 示例、单端点关闭重建说明、CI 与功能回归，然后合并 main。

本轮不更改 WLC/生成 ABI、线上帧格式、外部 Zephyr、产品依赖或固件；不发布 tag。
合并 main 后仍等待实板测试，不以模拟测试通过替代 H5 Ethernet/DMA/实时性验收。

## 明确保留的优化项

用户同意本轮暂不修复 Zephyr TX packet 池耗尽时的固定分配等待。该问题通过故意
占满池复现，不代表普通遥测/RPC 业务经常发生。后续分别测 packet 与数据块占用
峰值、突发量及资源释放时间，规划 TX/RX 容量，再修正耗尽时的非阻塞行为。
详见[平台限制](zephyr-udp-cn.md#必须保留的下一步zephyr-原生发送等待)。

## 实施状态

- M1 已提交；本轮重新运行 native_sim/64 的 socket、adapter、生成端点套件，
  3/3 配置、34/34 用例通过，无警告。日志：`build/udp-m1-checkpoint/`。
- M2 已通过 unit_testing、native_sim 32/64、Cortex-M3 QEMU：13/13 配置、
  150/150 用例，无警告。日志：`build/udp-m2-final/`。包含 timing 开关两种构建。
- M3 示例、重建连接验证与最终回归进行中。

## M2 发现与修复

UDP 黑洞测试发现一个通用 managed RPC 问题：请求在等待 ACK 时超时，RPC 槽位
已经释放，但底层取消发送没有终态事件，因此链路槽位未回收，后续调用一直排队。
现由 async owner service 收取已取消且物理 I/O 已结束的发送；等待中的 DMA 租约
仍保留，成功/失败发送仍走原事件路径。没有更改核心取消事件合同或生成 ABI。

真实 socket 转发测试同时丢请求、响应和双向 ACK，并复制请求，最终业务 handler
只执行一次，RPC 成功，300 次逻辑采样均被读取。数据年龄是调度步数断言，
不是实际网络延迟或性能 benchmark。另验证黑洞超时后同一个端点/调用池恢复。

## 可选插桩

`CONFIG_WIRELINK_ZEPHYR_UDP_TIMING=y` 为实际 send、receive 和 RX service
记录调用次数、累计/最大 elapsed cycles。不开启时不保留这些字段，也不读取计数器。
`rx_idle_passes` 表示首次接收未发现数据，可结合 service/预算/背压计数定位空转。

插桩使用 Zephyr `k_cycle_get_32()`，独立于协议时钟，不改变 RPC 截止时间。
它包含抢占和网络栈等待，并非纯 CPU 计费；service 不含后续协议/业务 dispatch，
也不能与其内部 receive 时间相加。单次测量必须短于 32 位计数器一圈；转换时间
须使用平台实际计数器频率，变频与 SMP 时另行验证计数器合同。
参考 [Zephyr kernel timing](https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html)。
