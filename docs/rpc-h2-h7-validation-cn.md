# H2：H7 平台等待、存储与 CPU 验证

日期：2026-09-07。状态：软件验收完成，连接 H7 CPU 失败，尚未烧录；等待用户拔插/RESET。
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

软件门槛已通过。恢复连接后直接覆盖旧测试固件，不备份；计划一次连接中完成一槽/四槽
首次运行和系统复位重跑。系统复位不当作断电冷启动。

脚本与镜像在 Windows `C:\Users\moonf\codings\wirelink-rpc-h2-20260907`；
本地记录目录为 `build/rpc-h2-20260907/`。`run.ps1` 验证 ELF 哈希后运行
`flash-and-check.jlink`；RTT 控制块 `0x24001010`、缓冲区 `0x24000010`，
读取 `0x24000000` 起 4352 B RAM，再恢复运行。`read-rtt.py` 校验捕获结构与 PASS 标记。
目前没有 H2 RTT 捕获或实测数字。

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

H2 全部实板功能/CPU 项和 M5/H3 仍待进行；产品依赖、main 和 tag 未改动。
