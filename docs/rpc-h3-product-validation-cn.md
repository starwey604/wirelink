# H3：ABI 25 产品物理链路验证记录

日期：2026-09-07。状态：**软件门槛通过；H3 物理 USB 部分通过，性能及板端证据尚未闭环**。
H1/H2 的板内成功不代替本记录；M5 集成与测试见[实施记录](rpc-usability-progress-cn.md)。

## 配对与边界

| 组件 | 验证快照 |
| --- | --- |
| Wirelink 核心 | `3df748826ad3a3b0dbdf642b68fe98221310343d` |
| WLC | `afa5dfd186be1f747dc6d0cbcfc54c79654bf5f7`，生成 ABI 25 |
| libflorid dev | `aadcc56faabea0ed9c65b119129ad5bc2dd5418e` |
| Ragtime 固件 | `5098c5b9c5b7dc7329c77ed3c43812a6c15a570f` |
| Ragtime host 诊断补丁 | `07a2998cc64fd93e019d6ce2c50cdab27cb9d337` |
| FCI protocol | `47d5323ae178042b0f9d83965230dabc9fc3fb6a` |
| 产品 Zephyr | `577e42ad187825cc30d4b51423c32872eb4cf054` |
| SDK / 固件编译器 | Zephyr SDK 1.0.1 / GCC 14.3.0 |
| Windows host | VS 18 / MSVC 19.51.36256，Astrial/libusb 1.0.30 |

FCI 保留显式 operation/status 字段映射，不切换托管 RPC 格式。此轮验证产品的
typed RPC、控制租约、遥测和关闭/重连集成，不把它说成普通托管 RPC 的线上互通测试。
普通托管 API 的所有权、等待器和固定池由 H1/H2 及新 UDP 教程另行覆盖。
`07a2998` 只在 host 打开失败时输出底层 USB 错误类别、数值、文本及设备序列号；
没有增加重试、修改超时或改变库、生成接口、固件和帧格式。Linux/MSVC 两端重新构建通过。

Windows 为 `moonf@192.168.8.5`，新目录
`C:\Users\moonf\codings\wirelink-rpc-h3-20260907`。
原工作区和长期测试构建未切换；libflorid 使用独立 detached worktree，
Ragtime 经 bundle 传递本次快照，未顺带推送原有八个未推提交。
没有 main 合并、tag 或发布，也未改写现有用户本地更改。

## 已构建的镜像

目标 `dm_mc02/stm32h723xx`，H723 550 MHz，I/D cache，速度优化和 LTO，
主线程栈 8192 B，10000 ticks/s。应用为 Ragtime 的
`firmware/tests/apps/willow_wirelink_hil`：500 Hz 模拟 Willow，vendor USB Bulk
`2fe3:574c`，没有电机/CAN/持久化/升级操作。名称和设置冒烟只修改测试 RAM。

| 镜像 | Flash / RAM | ELF SHA-256 |
| --- | --- | --- |
| 普通 HIL | 187088 / 75976 B | `1232f1eebc0e3f66bb32e7632db3131fe1577d1b6e233edab861194038d45b4e` |
| 一次性 USB 重枚举 | 187656 / 77384 B | `f72b10c79882b0233ba366bb604e4af66bedfa93dcca7318373d8e060bca9cdb` |

本地目录分别为 `build/rpc-m5-h3-h7` 和 `build/rpc-m5-h3-h7-reconnect`。
第二镜像在启动 5 秒后关闭 USB 1 秒再启用；这是故障注入，不用于性能测量。
RTT 控制块为 `0x24000000`，上行 4096 B，非阻塞 skip/drop。
周期统计使用 IRQ 开启的系统周期计数，含抢占，不能当纯线程 CPU，也不能简单相加嵌套区域。

## 软件验收与连接记录

libflorid Release 全示例、4 项测试各三轮和 ASan/UBSan 四项通过。
Ragtime 的 native_sim 32/64、Cortex-M3 QEMU 共 4 配置 80 用例通过。
Windows 新 host HIL 编译通过；辅助 persistence 目标的既有 Duration.hpp 转换告警
不属于 Wirelink 新生成代码，本轮不运行持久化测试。
独立 Astrial lifecycle HIL 启用 `ASTRIAL_BUILD_USB_HIL=ON` 和
`VCPKG_MANIFEST_FEATURES=usb`，使用新目录内的 libusb 1.0.30。
Wirelink 教程提交 `609ff8d` 的
[Host CI](https://github.com/starwey604/wirelink/actions/runs/34092154160) 和
[Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34092154052) 均已全部通过：
主 C 套件 50 配置 / 301 用例、生成示例 17 配置 / 17 用例；ESP32-S3 两配置仅构建通过，
不计作 ESP32-S3 实板验证。

14:44，核对 Windows ELF 哈希后，以 J-Link V9.72、SWD 100 kHz、探针
609799419 正常连接。探针识别正常、VTref 3.280 V；CPU attach 及 Commander 的
自动 connect-under-reset 失败，退出码 1。未执行 loadfile、擦写或运行新镜像。
当时板上仍是 H2 四槽镜像，随后请求用户重新拔插 H7/J-Link、RESET 松开。

日志 `build/rpc-m5-h3-flash-failed.log`，SHA-256：
`ffd5df497bae89c56db65d09dec60769d89b161304b0aa85d9df8ed58e526775`。
Windows 原件为验证目录下 `flash.log`；后续重试使用新日志名保留失败证据。

15:09:55（以下均 UTC+8），用户松开后正常连接成功：CPUID `411FC272`，小端、32 KB
I/D cache，DBGMCU `10016483`，VTref 3.279 V。核对 ELF 后，普通 HIL 擦写、校验通过，
Flash 操作 4.780 s；只覆盖镜像所在的 256 KB Flash 区域，没有全片擦除、option byte 操作
或旧固件备份。复位运行后，USB `2fe3:574c`、序列号 `323738373233511200260036` 枚举正常。
**当前板上为普通 H3 HIL，不再是 H2 镜像；一次性重枚举镜像尚未烧录。**

退出 Commander 后，再次连接采集 RTT 失败：具体 MCU 模式在 1 MHz / 100 kHz 下均报
`InitTarget() -1`。通用 Cortex-M7 logger 只进入搜索，未收到有效控制块或任何板端 CSV。
通用 Commander 后来给出大端、无 cache、71 个断点槽等矛盾信息，RAM 抓取全零，
该抓取判为无效，不能据此推断 RTT 初始化或 cache 故障。
15:17 另试了一次探针 RESET 引脚低/高脉冲，工具接受命令但 MCU attach 仍失败；
没有电气测量，不声称实际 NRST 波形有效。命令含义参考
[SEGGER Commander](https://kb.segger.com/J-Link_Commander)。此后不继续盲重试。

## 物理 USB 功能与生命周期

### 对抗性会话重连：通过

15:10:47，无 RTT 进程运行，执行：

```text
willow_wirelink_hil_host --period-us 2000 --warmup 10 --warmup-idle-ms 0 \
  --samples 100 --cycles 2 --reopen-delay-ms 20 --adversarial-reconnect
```

两轮各 100 次唯一命令回显，payload mismatch、序号 gap、foreign/reorder 均为零，
cleanup 均成功。session 为 `7698` / `47016`，首个 RPC 编号同为 `3315986515`，
未消费探测调用编号同为 `3315986517`，租约不同；上一会话保留租约/调用后，新会话探测完成。
设备信息/设置 RAM 冒烟与缓存压力检查通过，最慢 open / close 为 31.419 / 0.734 ms。
这里验证的是会话关闭后重开，**不等于 USB 物理断开/重枚举**。

### 产品 100 次生命周期：一轮失败，诊断重跑通过

15:12:38，`10 warmup + 100 samples`、100 cycles、20 ms reopen delay 的首轮，在
完成 cycle 0–48 后，第 50 次打开报 `endpoint=9`（LinkError）而中止。
已完成的 4900 次回显无错配、无 gap；未生成最终 lifecycle PASS。原 PowerShell 管道把
stderr 当作终止异常，最终错误只出现在执行输出，不在该轮 CSV 日志中。

随后补充 `07a2998` 错误信息及脚本原生退出码记录，**未加入自动重试**。
15:18:36–15:20:07，在相同参数、`LIBUSB_DEBUG=2`、无调试进程下重跑 100/100 通过：
10000 次唯一回显无错配、无 gap，全部 cleanup 成功；最慢 open / close 为
78.090 / 1.547 ms。约 200 ms 的短测量窗口受 Windows CPU 计时粒度影响，
其中 31.26% 的最差值不作长窗口 CPU 基线。

**首次连接失败尚未复现、根因未定，不能称为已修复。** 新日志里的 HID 打开警告不能
直接归因到本次 vendor Bulk 设备；后续若重现，先依据具体 USB 错误定位，不用重试隐藏失败。

### Astrial 底层 100 次生命周期：通过

15:24:54–15:25:01，`--cycles 100 --reopen-delay-ms 20`，固定同一设备序列号。
覆盖 RX 启停、重复 start 拒绝、重复 stop/close 幂等、取消完成、重新挂载接收、
关闭后调用返回 disconnected；100/100 全过。
open p50 / p99 / max 为 13.405 / 18.904 / 19.022 ms；stop p99 / max 为
0.334 / 0.349 ms，close p99 / max 为 1.208 / 1.254 ms。
这是 `reconnect_mode=0`，尚未覆盖真实拔出及重新枚举。

## 500 Hz 长窗口：正确性通过，严格性能门槛未全过

命令在两组三窗口中保持一致，不事后修改门槛：

```text
willow_wirelink_hil_host --period-us 2000 --warmup 1000 --warmup-idle-ms 600 \
  --samples 10000 --cycles 3 --reopen-delay-ms 100 \
  --strict-performance --max-cpu-percent 25
```

门槛为 rate ≥95%、gap ≤1000 ppm、p99 ≤4 ms、stall ≤20 ms、open/close ≤500 ms、
主机进程 CPU ≤25%。这里的 10000 次是命令—遥测闭环回显，不是 10000 个 RPC。

第一次测量期间通用 RTT logger 一直在失败搜索，没有有效板端数据。三个窗口均完成
10000 次唯一回显、零错配，gap 分别为 1 / 11 / 19，后两轮超标；CPU 为
9.53 / 7.10 / 7.71%，p99 为 2.194 / 2.155 / 2.145 ms。
该组保留为失败记录，**不是无探针干扰基线**。

15:23:16–15:24:27，确认没有 Commander/RTT logger 等调试进程，`LIBUSB_DEBUG=0`，
独立重跑得到下表：

| 窗口 | 唯一回显 | RTT p50 / p99 / max（ms） | 实际 Hz | Host CPU | gap / gap rate | 严格结果 |
| --- | ---: | --- | ---: | ---: | --- | --- |
| 0 | 10000 | 1.986 / 2.098 / 7.875 | 499.22 | 7.88% | 5 / 0.0499% | PASS |
| 1 | 10000 | 1.986 / 2.110 / 11.078 | 499.20 | 7.80% | 7 / 0.0699% | PASS |
| 2 | 10000 | 1.987 / 2.111 / 16.159 | 499.20 | 7.57% | 11 / 0.1098% | FAIL：gap |

三轮无 payload mismatch、foreign echo、重复/倒序，cleanup 成功；主机 transport
RX drop / TX failure、endpoint dispatch error、executor poll/service error 均为零。
最慢 open / close 为 50.660 / 0.802 ms。实际触发 1974 次 RX ring-tail pause，
这是接收缓冲区的暂停/恢复事件，不能冒充 RX overflow 或 Wirelink `rx_backpressure`。
`host_latest_coalesced=0`，`upstream_missing=23` 只将缺口定位到主机 latest 消费点之前，
**尚不能区分固件 tick 合并、发送忙、USB 层或主机调度原因**。

第三轮仍失败，不能将缺帧仅归因于 logger，也不能通过多跑几次挑绿色结果验收。
目前没有有效板端最终 RTT window；不得声称固件 overflow/dispatch/completion 为零，
亦不能用上述 Host CPU 替代 H7 的周期开销。此前 H2 板内数据保持独立。

## 尚待闭环

1. 恢复可靠调试连接，保持一次 Commander 会话尝试采集其 RTT Telnet 通道，避免烧录后
   退出再 attach；必须验证控制块/启动标记和各窗口最终记录，进程存活不等于采集成功。
   此流程尚未实测，接口依据 [SEGGER RTT](https://kb.segger.com/RTT)。
2. 同步保存主机缺帧统计及板端 `miss/sbusy/ovf/dispatch/complete` 和周期计数，定位上述
   500 Hz 严格失败；按证据修复后重跑固定的三个窗口，不放宽门槛。
3. 保留一次性打开失败为开放项；诊断重跑通过不是根因或修复证明。
4. 一次性重枚举镜像配合 Astrial HIL 验证一次断开、一次重连、RX 恢复，再用产品程序
   确认 RPC 恢复；最后恢复普通 HIL。当前尚未开始此项。

## 原始证据

以下原件均保存在 Windows 独立验证目录，本地副本在 `build/rpc-h3-20260907/`。
PowerShell 日志多为 UTF-16LE，哈希针对原始字节；失败日志不被成功重跑覆盖。

| 日志 | SHA-256 |
| --- | --- |
| `flash-after-release.log` | `3ee43d5952438a99a667ba6d173079b1ed8719e3c49a2f8c30e0d93d5407d37b` |
| `adversarial-no-rtt.log` | `98399eb62ae706f82e882caf7b919f37d653d2823fec064e2513f2f2726c232c` |
| `lifecycle-no-rtt.log`（49 轮后中止） | `4968245773592c0eeb1b83948c19897c1c44478a9ebd1a86ca52273fe26e2ed2` |
| `lifecycle-diagnostics.log` | `ee9006b598bdd9630d2dec66a4e36ad82963e2d4d6b7ab36a223c5f62633cce1` |
| `astrial-lifecycle-normal.log` | `30931178b916947f8d7687046620d03be4e941b0ca91f672d8eecc7f3c2abd33` |
| `performance-generic-rtt.log` | `2b3669f3a4873af0d552e29a09c2fd34c71bf2e2f616d9310c3a5a5bf97c5802` |
| `performance-clean-no-probe.log` | `706c503203bcad5e20b2fed889fd5b623b669c6d47bdf1d39f54c569cb3c8b71` |
