# H3：ABI 25 产品物理链路验证记录

日期：2026-09-07。状态：**软件门槛通过，H7 连接受阻，物理 USB 尚未验证**。
H1/H2 的板内成功不代替本记录；M5 集成与测试见[实施记录](rpc-usability-progress-cn.md)。

## 配对与边界

| 组件 | 验证快照 |
| --- | --- |
| Wirelink 核心 | `3df748826ad3a3b0dbdf642b68fe98221310343d` |
| WLC | `afa5dfd186be1f747dc6d0cbcfc54c79654bf5f7`，生成 ABI 25 |
| libflorid dev | `aadcc56faabea0ed9c65b119129ad5bc2dd5418e` |
| Ragtime dev | `5098c5b9c5b7dc7329c77ed3c43812a6c15a570f` |
| FCI protocol | `47d5323ae178042b0f9d83965230dabc9fc3fb6a` |
| 产品 Zephyr | `577e42ad187825cc30d4b51423c32872eb4cf054` |
| SDK / 固件编译器 | Zephyr SDK 1.0.1 / GCC 14.3.0 |
| Windows host | VS 18 / MSVC 19.51.36256，Astrial/libusb 1.0.30 |

FCI 保留显式 operation/status 字段映射，不切换托管 RPC 格式。此轮验证产品的
typed RPC、控制租约、遥测和关闭/重连集成，不把它说成普通托管 RPC 的线上互通测试。
普通托管 API 的所有权、等待器和固定池由 H1/H2 及新 UDP 教程另行覆盖。

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
独立 Astrial lifecycle HIL 也已编译：启用 `ASTRIAL_BUILD_USB_HIL=ON` 和
`VCPKG_MANIFEST_FEATURES=usb`，使用新目录内的 libusb 1.0.30。只构建，尚未运行。

14:44，核对 Windows ELF 哈希后，以 J-Link V9.72、SWD 100 kHz、探针
609799419 正常连接。探针识别正常、VTref 3.280 V；CPU attach 及 Commander 的
自动 connect-under-reset 失败，退出码 1。未执行 loadfile、擦写或运行新镜像。
板上仍保留 H2 四槽镜像，不反复盲重试，已请求用户重新拔插 H7/J-Link、RESET 松开。

日志 `build/rpc-m5-h3-flash-failed.log`，SHA-256：
`ffd5df497bae89c56db65d09dec60769d89b161304b0aa85d9df8ed58e526775`。
Windows 原件为验证目录下 `flash.log`；后续重试使用新日志名保留失败证据。

## 恢复连接后的顺序与验收项

1. 普通镜像核对哈希、烧录校验，启动独立 RTT 采集；先确认启动标记和 USB 身份。
2. 两轮对抗性重连：相同 operation seed、不同 session/租约，上一轮保留未消费 RPC；
   设备信息/设置、缓存压力及新 session 结果必须正确。
3. 100 次 open/握手/控制/release/stop/quiesce/close，每轮 10 warmup + 100 唯一回显；
   重复 stop/quiesce 幂等，记录资源与关闭延迟。
4. 三个 500 Hz 性能窗口：各 1000 warmup、600 ms idle、10000 样本；保持既有严格门槛
   rate ≥95%、gap ≤1000 ppm、p99 ≤2 个周期、stall ≤10 个周期、open/close ≤500 ms，
   主机进程 CPU ≤25%。失败时记录并归因，不事后放宽门槛冒充通过。
5. 保存主机 CSV 和每个设备窗口 final=1 RTT；核对唯一回显、无 payload mismatch、
   overflow/dispatch/completion 错误。背压与合并计数单独记录；没有实际触发就不声称覆盖。
6. 一次性 USB 重枚举镜像配合 Astrial lifecycle HIL，验证一次断开、一次重连、RX 重新挂载，
   再以产品程序确认 RPC 可恢复；最后恢复普通 HIL 镜像。

当前这些实测项全部待执行，无 H3 延迟、CPU 或物理断连通过结论。
