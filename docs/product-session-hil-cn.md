# ABI 26：Willow H7 USB 实板验证

2026-09-07，本机 Linux + STM32H723 / dm_mc02。**功能通过，严格性能验收未通过**。
承接 [产品同步记录](product-session-sync-cn.md)。不改 API/代码、不合 main、不发布；
所有测试使用同一 HIL 镜像和主机可执行文件，没有调宽门槛或重复跑到全绿。

## 配对与烧录

- Wirelink `10083e0`，WLC `c6b6a8f` / ABI 26，FCI `47d5323`（显式字段映射）。
- libflorid `b2a21ce`，Ragtime `2089ae8`；Zephyr `577e42ad1878`，SDK 1.0.1。
- Host：Linux `7.2.2-1-cachyos`，GCC 16.2.1；本机还有桌面及其他开发任务。
- H7：550 MHz，真实 STM32 RNG，USB SN `323738373233511200260036`。
  HIL 不启用电机/CAN/存储；不是带真实机械臂运行的产品固件验收。
- ELF：`Ragtime_Firmwares/build/willow-hil-session-abi26/zephyr/zephyr.elf`，
  SHA-256 `e665bb9e76c306b4ebe8ddb7c967b90c9bc55e1c99e4812713a07f643e69801b`；
  Flash 167096 B / RAM 76360 B。
- Host：`build/willow-hil-host-abi26/willow_wirelink_hil_host`，SHA-256
  `cc81c07863cd67ef77f1f3b67acd0c3212887e509bcbff1651ef1fae901963fe`。

拔插后旧 Commander 的 USB handle 已失效，重开会话后等待用户松开 RESET，成功识别
Cortex-M7 r1p2 / CPUID `411FC272`。直接烧录并校验通过，没有备份旧测试固件。
RTT 通过同一 Commander 的 `127.0.0.1:19021` 采集，有完整启动标记。

第二批采集显式设置 `exec SetAllowStopMode = 0`，禁止 RTT 通过暂停目标访问内存，
命令依据 [SEGGER 说明](https://kb.segger.com/J-Link_Command_Strings#SetAllowStopMode)。
第三批先软件复位 H7，确认新的启动标记和 USB 重枚举，再退出 Commander 和 RTT，
没有活动的调试连接。板上最终保留本次普通 HIL 镜像，仍在运行。

## 功能与生命周期

- 九个长窗口：每窗 10000 次唯一命令—遥测回显，合计 90000 次，全部正确；
  这不是 90000 个 RPC。身份/设置事务、typed RPC、诊断接收及 cleanup 均通过。
- 100 次完整 endpoint/transport 重建：100/100 PASS，100 个默认 session 均非零且互异。
  包含 open、握手、100 次回显、release、重复 stop/quiesce 和 close；
  最慢 open/close 为 16.505/1.585 ms。生命周期不使用短窗口严格性能门槛；
  其中最长回显 179.698 ms，不能因为功能 PASS 而省略。
- 对抗性重连：2/2 PASS。session `7698` / `47016` 刻意使用相同首调用编号
  `3315986515`；第一轮遗留未消费 RPC 和租约后断开，第二轮取得不同租约，probe 完成。
  这是现有映射 RPC 场景测试，不代表完整托管 RPC v2 旧响应隔离证明。
- 未执行断电 NVS 持久化或完整 Willow 电机业务验证；此前软件构建不替代这些测试。

## 三批性能结果：保留全部窗口

共同参数（CPU 百分比仅记录，没有设置其阈值）：

```sh
willow_wirelink_hil_host --serial 323738373233511200260036 \
  --period-us 2000 --warmup 1000 --warmup-idle-ms 600 --samples 10000 \
  --cycles 3 --reopen-delay-ms 100 --strict-performance --require-diagnostics
```

门槛保持：频率至少 475 Hz、gap 不超过 0.1%、p99 ≤ 4 ms、最大值 ≤ 20 ms、
open/close ≤ 500 ms。下表每行的正确性和测量有效性均 PASS。

| 批次/窗口 | Hz | RTT p99 / max（ms） | gap | Host 进程 CPU | 性能 |
| --- | ---: | --- | ---: | ---: | --- |
| 初次 RTT / 0 | 469.70 | 2.507 / 70.647 | 6.0767% | 5.05% | FAIL |
| 初次 RTT / 1 | 374.77 | 38.466 / 109.248 | 24.3102% | 4.55% | FAIL |
| 初次 RTT / 2 | 500.39 | 2.085 / 3.033 | 0 | 5.14% | PASS |
| 禁用 stop-mode / 0 | 499.69 | 2.086 / 2.950 | 0 | 5.11% | PASS |
| 禁用 stop-mode / 1 | 298.67 | 63.336 / 120.473 | 39.0671% | 4.31% | FAIL |
| 禁用 stop-mode / 2 | 500.06 | 2.084 / 2.985 | 0 | 5.21% | PASS |
| 无探针 / 0 | 477.74 | 2.241 / 75.602 | 4.1754% | 4.66% | FAIL |
| 无探针 / 1 | 500.00 | 2.096 / 3.308 | 0 | 4.86% | PASS |
| 无探针 / 2 | 499.94 | 2.080 / 3.599 | 0 | 5.29% | PASS |

三次命令均以 exit 2 结束，不能记为严格性能通过。正常窗口约 500 Hz、p50 约 1.99 ms。
失败时无 payload mismatch、foreign echo、RX drop、TX failure、dispatch/poll/service error。
无探针失败窗有 437 个状态 gap，其中 395 个属于 host LATEST 合并，42 个上游缺口；
10000 条业务命令仍全部取得唯一正确回显。LATEST gap 不等同于可靠消息丢失。

## 板端成本与归因边界

第二批对应 RTT 窗口 10/12/14，均取得 12 类完整最终记录。所有窗口的 USB error、
feed overflow、dispatch/completion error、漏 tick、owner deadline miss、业务超时均为零。

| 区域 | 平均时间/调用范围 | 本批记录的最大时间 |
| --- | --- | ---: |
| USB RX callback | 2.471–2.477 µs | 8.444 µs |
| USB TX submit | 4.283–4.296 µs | 21.669 µs |
| 协议 service | 8.020–9.451 µs | 72.396 µs |
| 遥测 service | 8.456–9.381 µs | 38.371 µs |
| owner pass | 20.551–21.158 µs | 81.216 µs |

`owner` cycles/窗口墙时为 2.6063–3.1516%；包含抢占和收尾 idle，不是纯线程 CPU。
各区域嵌套，不能相加；USB adapter 的 max 是启动以来的最大值，非每窗独立峰值。
这些数据不足以把主机几十毫秒长尾解释成固件 service CPU 开销。

**待收敛项：Linux 主机/USB 接收路径的偶发长尾。** 关闭探针后仍能复现，排除
“只关闭 RTT 就修好”的判断。初期观察到其他编译任务，但未做逐事件关联，不能把它们
直接认定为根因。补充的独立 2 ms sleep 诊断（10000 次）额外延迟 p99 48.585 µs、
最大 958.177 µs，没有 >10 ms 样本；诊断未覆盖同一个坏窗口，不能据此排除调度问题。

下一步应给 host 的 libusb completion → RX 发布 → executor 唤醒/dispatch → 业务回调
添加可选时间戳采样，捕获坏窗口，再用同机旧 host/当前 host 对照定位。不要先改 API、
扩大 deadline、隐藏 gap，或把本次失败写成“已修复”。在这项收敛前，不替换长期测试基线。

## 原始记录

均在 `Ragtime_Firmwares/build/`，未提交大日志：

| 文件 | SHA-256 |
| --- | --- |
| `session-abi26-jlink-held-reset.log` | `60ef778a968e2ed17be1b88919194ac7850e3f62e152ef7179894a154168334e` |
| `session-abi26-hil-performance.log` | `1acb90a8af6db02f9476c77734c5946d598158ae70c8389f4ad478f332e53013` |
| `session-abi26-hil-performance-controlled.log` | `a97b99e930d47e4c5b1b87e3e2d9a9f7b0197992e22d9815ba782122d8544e37` |
| `session-abi26-hil-performance-no-probe.log` | `1d9aec18e77bfaf96b00f302d374719999da313d179561fead83f16c863bb8cd` |
| `session-abi26-hil-lifecycle.log` | `6c5e07ad4d7a98bd94216c909969b7aded15e0e5236a1b9bcc9608e1a0f0b17d` |
| `session-abi26-hil-adversarial.log` | `01bcf3a4294beb278c91cf6153c4c0c080f54238ad330f8f5ee6c39692566008` |
| `session-abi26-hil-rtt.log` | `8ce20d20112d3a5e02fd5ac6797fe91c8b13f90763d8fec81faae798636657dd` |
| `session-abi26-hil-rtt-controlled.log` | `44756b23b5b175cbba3ed449a5fc7149de2070d5672abe053ba33ac170728490` |
