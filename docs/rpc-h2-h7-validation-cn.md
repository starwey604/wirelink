# H2：H7 平台等待、存储与 CPU 验证

日期：2026-09-07。状态：H2 完成，一槽/四槽首次运行与系统复位重跑四次全部通过。
这是 M3/M4 的独立样例验收，不是 M5/H3 产品物理链路测试。

## 范围与软件前置条件

[`rpc_platform`](../samples/zephyr/rpc_platform/README.md) 用两个 Zephyr 任务各自拥有
一个端点，通过有界 RAM packet 队列通信。验证一/四槽、固定池创建失败回滚与池耗尽、
100 次同步字符串调用、拒绝、2031-byte 响应、20 次异步通知、另一任务中断长等待，
以及 stop/join 后销毁、已复制结果存活、热路径零端点分配器调用。

本地核心 50 配置/301 用例、Host 13 项、安装包 4 项、ASan/UBSan、TSan 均通过；
最终 H2 模拟矩阵 6/6 和时钟样例 4/4 通过。详细命令、日志及首轮失败修正见
[实施记录](rpc-usability-progress-cn.md)。最终配对的 Host、Zephyr、WLC CI 全部通过；
远端完整样例矩阵 17/17 通过。

## 固定镜像与环境

- Wirelink `3df748826ad3a3b0dbdf642b68fe98221310343d`。
- WLC `afa5dfd186be1f747dc6d0cbcfc54c79654bf5f7`，生成 ABI 25；后续仅 README 修正。
- 板型 `dm_mc02/stm32h723xx`，STM32H723VG；外部 Ragtime 板定义只用于构建。
- Zephyr `v4.4.0-11610-gbd8c15382376`，SDK 1.0.1 / GCC 14.3.0，速度优化。
  配置 550 MHz、I/D cache 开启、tickless、10000 ticks/s；主线程栈 8192 B，服务线程 4096 B。
- Windows `moonf@192.168.8.5`，Commander 9.72，J-Link `609799419`。
  仅 RTT 与 RAM 通信，没有执行器控制、USB/UART 流量或产品升级写入。

| 容量 | endpoint 字节数（ELF 符号推导） | 完整样例 Flash | 完整样例 RAM |
| --- | ---: | ---: | ---: |
| 1 | 16568 | 76792 B | 72448 B |
| 4 | 29856 | 76816 B | 99072 B |

RAM 包含两个端点、测试收包队列、任务栈、Zephyr 与测量保存值，不是库本体大小。
相对 H1 的完整镜像差异不能全部归因为分配器；本样例增加了 RTOS 任务和独立 packet 队列。

ELF SHA-256，烧录脚本会在 Windows 核对，不匹配则停止：

- 一槽：`23a5be60e0bc4d2beeeb6bf85ae145e061ae3b5595b7f025f3cb763b81aed937`
- 四槽：`1bc667df1cdebe4c7ce89bb146e8dce826bb6aa5d16a36f6976a413f96571532`

## 测量合同

IRQ 开启的系统周期钟记录 100 次 RAM 往返的 p50/p95/p99，包含任务等待和调度，
不是物理链路延迟。DWT 记录包含完成通知的 owner pass，可能含中断开销，
不能称为隔离的 callback 成本。

其他任务退出后，在有界 IRQ-off 批次测量 idle step（128 次）、2031-byte encode/decode
（各 32 次）、volatile 自持值复制（128 次）。这些 CPU 路径使用 DWT CYCCNT，
不使用屏蔽中断时可能漏计回绕的 SysTick；未扣除读计数器开销。
volatile 复制防止优化消除，但不代表普通结构体赋值的最优路径。
idle 断言每轮只读一次业务时钟，栈报告来自 Zephyr 已使用区域扫描。
native_sim/QEMU 的周期与栈数字只用于功能诊断，不进入 H7 性能结论。

## 执行与证据

软件门槛通过后，14:16（UTC+8）在同一连接中直接烧录一槽、再烧四槽，各运行并系统复位重跑。
Flash 校验均通过，每次影响 `0x08000000` 起一个 128 KiB 区域；没有旧固件备份、
全片擦除或 option byte 修改。系统复位使用 AIRCR.SYSRESETREQ，不当作断电冷启动。

脚本与镜像在 Windows `C:\Users\moonf\codings\wirelink-rpc-h2-20260907`；
本地记录目录为 `build/rpc-h2-20260907/`。`run.ps1` 验证 ELF 哈希后运行
`flash-and-check.jlink`；RTT 控制块 `0x24001010`、缓冲区 `0x24000010`，
读取 `0x24000000` 起 4352 B RAM，再恢复运行。`read-rtt.py` 校验捕获结构与 PASS 标记。
四份 RTT 捕获均含完整启动记录及单个 `RPC_H2 ALL PASS`，无 FAIL/FATAL；
读偏移 0、写偏移 494，没有缓冲区回绕。同容量的两次输出和捕获哈希完全相同。
本轮无需修改实现；当前 Flash 保留四槽 ABI 25 独立测试镜像，CPU 恢复运行，调试器已退出。

### 功能与资源结果

一/四槽每次均为 122 次正常 RPC（含一次业务拒绝及一次大响应）、20 次异步通知；
黑洞中的额外调用被另一任务停止等待并唯一取消。100 次连续字符串调用全部得到预期值。
请求与回包在 RAM 队列交换，未使用 USB/UART/DMA。

RPC 阶段端点分配器调用为 0；含故障注入及后续 CPU 测试的全程为 6 次申请尝试、
4 次配对释放，其中两次申请返回 NULL，最终池占用 0。失败初始化回滚、池满拒绝、
stop/join/destroy，以及销毁后保存的字符串/bytes 响应均通过断言。
client/server 等待器调用数 245/2；这不是上下文切换次数，也不能据此推算 idle CPU 利用率。

主/服务线程未使用栈为 6236/2732 B，按配置容量折算本样例已用约 1956/1364 B。
这是本次路径的高水位观测，不是所有业务 handler、中断嵌套或输入下的栈预算保证。
idle 每轮读取一次业务时钟的断言通过。

### CPU 与 RAM 往返观测

单位为周期（括号内按 550 MHz 换算为 μs）；每种容量的复位重跑结果相同。

| 测量 | 1 槽 | 4 槽 |
| --- | ---: | ---: |
| idle step，IRQ off | 1275（2.32） | 1358（2.47） |
| encode 2031 B，IRQ off | 2107（3.83） | 2068（3.76） |
| decode 2031 B，IRQ off | 4038（7.34） | 3853（7.01） |
| volatile 大值复制，IRQ off | 920（1.67） | 928（1.69） |
| 含完成通知的 owner pass，IRQ on | 6749（12.27） | 6942（12.62） |
| RAM RPC p50，含等待/调度 | 35559（64.65） | 36630（66.60） |
| RAM RPC p95，含等待/调度 | 35807（65.10） | 36872（67.04） |
| RAM RPC p99，含等待/调度 | 36633（66.61） | 37306（67.83） |

这里只观测两个构建在同一硬件条件下的差别，不能把微小差异归因于唯一机制；
代码/数据布局和缓存也会影响结果。四槽相比一槽增加 13288 B/endpoint，idle 增加 83 周期，
RAM 往返中位数增加 1071 周期。本样例近 2 KiB 响应的存储成本显著，
RAM 受限产品应审视消息上限及容量，而非照搬四槽压力配置。
没有相同测量方法的旧 ABI 基线，不宣称本轮相比旧实现加速或退化多少。

### 成功证据

| 文件 | SHA-256 |
| --- | --- |
| `flash-after-release.log` | `c09b3b8438289ed615ae4480bdd0c61a9e17b702f529192fce013fd30fac1aba` |
| `one-first.bin` / `one-reset.bin` | `d1674ff6d92f52e9532bb46ae482f0d4094fdfc6ac85539166e82efda87d7dbc` |
| `four-first.bin` / `four-reset.bin` | `a71cb488f6b691f9cfcea3fc22fc4727c7ef2b8f0917c4449d733173fd45b6d6` |

连接读到 Cortex-M7 r1p2、DBGMCU `0x10016483`、Flash 容量 `0x0400`，目标电压 3.308 V。
100 kHz SWD 建连后切到 1 MHz；2 秒为调试器运行后采集等待，不是 RPC 延迟。

### 连接阻塞（14:11，UTC+8）

两种 ELF 的 Windows 哈希均与本地一致。首次连接探针成功，电压 3.285 V，
DAP 初始化成功，但无法 attach CPU；Commander 自动 connect-under-reset 也失败，
`InitTarget()` 返回 -1。没有执行 `loadfile`，Flash 未改写；不能将此记作 RPC 功能失败。

确认无其他调试进程占用后，仅对 `USB\VID_1366&PID_0101\000609799419` 执行一次
`pnputil /restart-device`。Windows 报告重启成功，设备状态 OK；重试电压 3.282 V，
仍在相同 CPU attach 阶段失败。设备绑定重启不是物理断电，未继续盲重试。
需要用户断电重插 H7/J-Link，必要时按住 RESET 后继续。

首次与重试分别保存 `flash.log` / `flash-retry1.log`，设备重启保存 `restart-jlink.log`，
本地与 Windows 路径如上。后续重试应给 `run.ps1 -LogName` 指定新名称，保留本次证据。

| 日志 | SHA-256 |
| --- | --- |
| `flash.log` | `d5dd95181cbdb78f0072a287905e97fce436f675195306b282533812f43f5d07` |
| `flash-retry1.log` | `6c3f34413b294043242a7c99c6099a4a7e337d7e75dc74acf4cf07b190ec4bd5` |
| `restart-jlink.log` | `296e95872127454c17c75437eae32c2be0250066e338464abd07bfb29ce9ffce` |

以上为连接恢复前的历史记录；后续恢复与成功结果见上文，产品依赖、main 和 tag 未改动。

14:15 用户确认按住 RESET 后再次尝试：探针电压 3.308 V、DAP 初始化成功，
但日志明确为 `Timeout while waiting for CPU to halt`，自动 connect-under-reset 仍失败。
该次未执行 `loadfile`；记录为 `flash-after-reset.log`，SHA-256
`3a7ee13885f2731db1e19c9ae1938eb2324b5ceeb2dfaa3a755ad08645b4de90`。
用户随后确认松开 RESET，14:16 正常连接恢复，没有自动 connect-under-reset，
完成上述四次验证。H2 清单无剩余项；M5 产品迁移及 H3 物理链路仍待推进。
