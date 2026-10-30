# 静态端点内核：K3 实施记录

日期：2026-10-31。用户已明确要求设定持续目标完成 K6，除必要停止条件外继续推进。对应[执行计划](static-endpoint-kernel-refactor-plan-20261030-cn.md)；当前 **K3 进行中，K4 尚未登记／测量，K5/K6 尚未完成**。默认产品仍是旧实现。本文记录已完成点，不替代最终验收报告。

## 已完成点

| 仓库 | 提交 | 内容 |
| --- | --- | --- |
| Wirelink | `8908689` | C++ 头文件消费；C 持有 atomic port，C++ 不假设 std::atomic ABI；实际混合语言生成消费者。 |
| WLC | `7d3c230` | 生成 kernel 头声明使用 C linkage。 |
| Wirelink | `2336edc` | Astrial USB 直接绑定 packet port，保留一 RX／一 TX、借用与关闭 join。 |
| Wirelink | `c1f2212` | 已接收 TX 的跨类型 FIFO，修复 Progress 被 Control STOP 越过的问题；九个索引引用原 TX owner，没有增加 payload 或 RPC 表。 |
| Wirelink | `aba8fa0` | 非 benchmark schema 的嵌套 message、embedded-NUL string、定长 packed 数组、不同容量及未知字段恢复。 |
| benchmark | `27d517b` | 新 host 编译原 workload/observer，真实业务映射使用同一个 C 内核与生成端点。 |
| Wirelink | `be2b598` | 独立 COBS/CRC16 与 length16/NONE 连接，公共 framing 工具作测试 oracle；无旧执行引擎依赖。 |
| WLC | `af819f7` | 显式选择部署并生成 transport 连接，非 benchmark RPC 经两种 framing 实际运行。 |
| Wirelink | `0a2327d` | 独立有界 SPSC 字节暂存，producer 不解析；owner 的 COBS 恢复显式报告 gap。 |
| Wirelink | `22a53f1` | Zephyr IRQ/DMA/polling UART 接到 packet/字节暂存边界；实际 adapter 软件 fixture。 |

## 实板资格与已发现问题

证据位于 `../wirelink-static-kernel-k3k4-20261031/`；旧 K0–K2 证据只读保留。新 USB host/new device 的 CPU-off/on smoke 均通过，含 100 RPC、1000 paced Sample、32/120/512/1024 B 三方向短 stream；两组双向 63/64/65、127/128/129、511/512/513 编码边界也通过。它们均标为 qualification_only，不能充当 K4 的 10000 RPC／5 s 窗口完整矩阵。

第一次新 host/旧 K2-B smoke 报设备业务错误 10，transport error 为零。定位到旧 TX 轮转可让后续 Control 请求越过已接收的普通 Progress。固定九索引 FIFO 后，用相同旧 K2-B 及相同业务重跑全部错误归零；随后新 device 重建并完成上述 CPU-off/on 资格。新增 core 回归覆盖 BUSY 队头保留、取消未发送调用、同一 TX 索引复用后的顺序。没有修改业务账本或等待逻辑掩盖问题。

另一份资格在最后 stream 被 USB Hub 整体断连中断，内核日志同时记录 `3-2` Hub、`3-2.3` 串口桥及 `3-2.4` 板卡断开／重枚举。失败原始捕获与 journal 保留，未纳入性能结果。

新设备身份 `static-kernel-k3-20261031-B-o0/o1`。实际 endpoint 为 **4408 B**（FIFO 前 K2 为 4400 B），port 88 B、USB adapter 152 B；没有新增 payload 池。相对冻结 A 的完整配置仍只有原先登记的五个功能开关差异，CPU/cache/USB/栈/heap/LTO/SMP 条件不变。framing 和其他 adapter 是独立目标，不因此进入 native USB 固件。

## 软件验证与剩余工作

GCC 和 Clang ASan/UBSan 的原有核心／生成消费者、混合 C/C++、真实 Astrial adapter、framing 和字节暂存 fixture 通过；并发暂存覆盖 100 万字节。WLC 完整 test 与 all-targets/all-features clippy 通过，新增部署测试在相同 C 内核上执行真实请求／handler／响应，而非独立 RPC engine。

UART 的实际 IRQ、DMA、polling adapter 在 native_sim、Cortex-M3、RISC-V 共 12 个 case/platform 执行通过。覆盖 ISR 只暂存、分片与背压、物理 consumed/drained、DMA 两缓冲真实归还、旧 callback、满 FIFO 关闭及已进入 callback 的 join。Polling 新合同要求平台 idle 查询，停止时无法确认 drain 就保留存储并返回 WOULD_BLOCK。尚无 UART 硬件吞吐或物理时序结论。

正在完成 UDP、串口 host、剩余生命周期／安装消费和迁移清单。K3 完整保留合同与 adapter 验证之后登记 K4：冻结旧 A、新 host/new device B、当前 libcsp C，off/on 三轮 18 份完整捕获；保留 K2 的 host Sample CPU 单次异常和尾部不可判定项。再进入 K5 默认产品与消费者收敛，最后完成 K6 独立重放及交回。
