# 新内核 RPC 与应用遥测模型

[English](README.md)。构建和采集命令见英文页，需要匹配的本地 WLC 0.13。

模型运行真实生成的双角色内核和 packet port，每组 2400 个确定性 tick；分别显式
连接 native packet、COBS/CRC16 和 length16/NONE。Echo／Telemetry 业务 schema
和消息 ID 不变，profile 改为 v2。它不预测 USB／UART 延迟、CPU 或 OS 调度。

七组场景覆盖纯遥测、正常 RPC、丢一个请求、丢一个响应、有限 BUSY、丢包加背压，
以及饱和遥测与 u32 时钟回绕。每方向五个应用 job，超时后应用显式重试，最多三次，
每次取得新的 kernel operation。丢响应会重复执行业务：模型明确校验这一事实，
不再假定框架 replay cache、回滚或 exactly-once。实际副作用需要业务持久化幂等 ID。

应用自行保留一个最新未接纳遥测值；kernel 接纳时完成 snapshot。物理驱动模型在
CONSUMED 前真实保留源数据，之后持有自己的传输副本，到 peer 接纳或显式丢包后
才发布 DRAINED。BUSY 等待有限资源通知，close 真正放弃未完成传输再发布 TX_ERROR，
不会伪造成功 drain。模型不分配堆、不开线程或 socket，也不 sleep。

新 format 2 共 42 条完整记录，使用 `compare_kernel.py`。解析器检查缺行、重复行、
非法计数、缺少故障注入，以及丢响应时谎称只执行业务一次的记录。age／gap 覆盖接收方
最后应用值与静默尾部；RPC 耗时包括应用重试。`--check` 额外检查场景明确给出的
串行化与背压界限。比较前必须核对工作负载源码、编译器、flags 和二进制 hash。

Zephyr 集成测试复用同一模型。固件 wrapper 的 `mixed_kernel_fw_*_v1` 周期包含模型
复制、丢包检查与应用 bookkeeping，只是 IRQ 屏蔽的固有成本诊断，不是端到端 CPU，
不能与旧 retry 引擎数字比较。当前 RPC／Sample 实板证据由独立静态内核 USB workload
提供；模型测试本身不需刷板。

当前固件采集使用 `compare_kernel_firmware.py before.log after.log`，必须含完整 42 条 traffic 与 21 组 cycle，核对重复执行计数，拒绝复位、缺失、混用旧格式和 clock/layout 上下文变化。模拟毫秒和 IRQ-masked 模型 cycle 分开，不代表物理吞吐或旧 ARQ CPU 对照。
