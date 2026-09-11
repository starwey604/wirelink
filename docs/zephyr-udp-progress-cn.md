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
  3/3 配置、34/34 用例通过，无编译器警告。日志：`build/udp-m1-checkpoint/`。
- M2 已通过 unit_testing、native_sim 32/64、Cortex-M3 QEMU：13/13 配置、
  150/150 用例，无编译器警告。日志：`build/udp-m2-final/`。包含 timing 开关两种构建。
- M3 实现及软件验收完成：扩大 Zephyr 回归 60/60 配置、452/452 用例通过，
  19 个不适用配置按现有平台过滤；无编译器警告。日志：`build/udp-m3-final/`。
  构建工具有 Ninja 不采用 pipe jobserver 的提示，不影响测试结果。

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

## M3 示例与验收

[两设备示例](../samples/zephyr/udp_peer/README-cn.md)提供独立的 Zephyr client/server，
以及桌面 C client/server（平台层用已有 Asio adapter）。双方与 Ztest 共用
`examples/05_zephyr_udp/` 中唯一一份 schema/profile；没有新增编解码实现。
原 localhost 教程保留原命令；新示例显式配置本地/对端 IP 和端口。

| 门槛 | 结果 | 本地记录 |
| --- | --- | --- |
| unit/integration + RPC ownership/platform + UDP，四种测试平台 | 60/60 配置，452/452 用例通过 | `build/udp-m3-final/` |
| 生成端点故障/重建回归，native 32/64 + QEMU | 3/3 配置，21/21 用例通过 | `build/udp-m3-reconnect/` |
| Zephyr 独立 client/server，native 32/64 | 4/4 构建通过，仅构建、未跨 TAP 运行 | `build/udp-m3-peer-build/` |
| GCC Release 主机，包括新进程对和 C/C++/Python 消费 | 19/19 通过 | `build/udp-m3-host/Testing/Temporary/LastTest.log` |
| Clang ASan+UBSan 主机 | 18/18 通过，启用 leak 检查 | `build/udp-m3-sanitize/Testing/Temporary/LastTest.log` |

Python 薄 FFI 另行预加载 Clang ASan runtime 通过，针对未插桩的 Python 解释器关闭
leak 检查；不能把它算作通过完整进程 leak 验收。以上没有执行任何性能 benchmark。
Zephyr 使用 `e4e6910cc19b7f11eada127e54c0b5248f413799` / SDK 1.0.1，WLC 为
`85b1bdc`（0.7.0-dev / ABI 32）；未改动这些外部工作区。

同一端点内存关闭重建的用例，验证未完成 RPC 取消一次、旧句柄失效、旧响应不能
完成新会话的调用。它不是 USB↔UDP 热迁移；旧请求/遥测的新鲜度、防重放和双方
切换协调仍是明确边界，不能仅换 session 就声称解决。

CI 已加入两个 Zephyr 角色构建；新增主机进程测试由既有 Ubuntu/macOS/Windows
tutorial CTest 路径自动执行。远程 CI 尚未触发，本轮未 push。

提交按范围拆分：`fc04fbc` 为 M1 默认端点适配器，`7a03b57` 为取消发送回收及
故障回归，`6362962` 为可选插桩。M3 示例/文档单独提交，按用户授权合入本地 main，
保留实验状态，不将合并等同于实板或实时性验收。`AGENTS.md` 与独立 `wlc/`
工作区保持原样，不纳入本轮提交。

完整 Zephyr 回归从初始化好的 workspace 运行，替换 Wirelink/WLC/输出路径：

```sh
west twister \
  -T /path/to/wirelink/tests/zephyr/unit \
  -T /path/to/wirelink/tests/zephyr/integration \
  -T /path/to/wirelink/samples/zephyr/rpc_usability \
  -T /path/to/wirelink/samples/zephyr/rpc_platform \
  -T /path/to/wirelink/samples/zephyr/udp_validation \
  -p unit_testing -p native_sim -p native_sim/native/64 -p qemu_cortex_m3 \
  -x=WIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -x=WIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -j 2 --inline-logs --outdir /path/to/build/udp-m3-final
```

下一步为实板功能及 H5 Ethernet 的资源/实时性验收；不自动烧录 Willow、不改产品
依赖、不发布 tag。packet 池等待、驱动锁、DMA 描述符背压及真实断链继续保留。
