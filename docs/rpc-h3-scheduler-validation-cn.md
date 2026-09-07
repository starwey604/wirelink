# H3 续验：产品遥测调度与板端证据

日期：2026-09-07。状态：**本轮修正版 H3 验收通过：配对/无探针性能、生命周期、USB 重枚举及恢复均通过**。
本记录承接[首轮 H3](rpc-h3-product-validation-cn.md)，保留首轮失败，不能用后来的绿色
结果倒推早期故障已经全部修复。H1/H2 的托管 RPC 验证仍独立于 FCI 显式映射模式。

## 修正范围与配对

保持 Wirelink `3df7488`、WLC `afa5dfd` / ABI 25、libflorid `aadcc56`、FCI `47d5323`、
Zephyr `577e42ad1878` 不变。Ragtime 增量为
`4d3efb203ab54a6a8e1253f7e43e97175c823b91`，在已有 `07a2998` USB 错误诊断之上：

- HIL 复用正式 Willow 已有的 `ArmTelemetryScheduler`，每类消息保留最新快照，
  忙时等待后续 owner 唤醒，状态/诊断轮流发送；未另建动态队列、线程或时钟。
- 调度器只增加成功提交及 busy 统计，不改变正式产品已有的调度行为。
- host 增加 `--require-diagnostics`，缺少实际诊断回调就判正确性失败。
- 板端记录升级为 v2，区分发布快照与编码/发送成本，及忙、替换、硬错误。

没有修改 Wirelink 核心 API、生成 ABI 或帧格式，没有 main 合并、tag 或发布。
Ragtime 新提交仍只通过 bundle 传到 Windows 独立 worktree，未顺带推送八个既有未推提交。

| H7 镜像 | Flash / RAM（B） | ELF SHA-256 |
| --- | --- | --- |
| 调度修正普通 HIL | 187872 / 76360 | `885d1df26e7b7fcf7146588d1e6d860facd47a3d23255bb579e285dd211fcc4d` |
| 调度修正重枚举 HIL | 188400 / 77640 | `6978fe04ab16ff4212291430b766631ac38bc7c242370b0c38befdc0ed68222c` |

普通镜像相对首轮增加 Flash 784 B、RAM 384 B，含调度器快照和统计。
目标仍为 H723 550 MHz、I/D cache、LTO、8192 B 主线程栈、10000 ticks/s；
没有电机/CAN/持久化操作。两镜像均核对 Windows 哈希后烧录校验通过。
重枚举验证结束后，已恢复普通修正版镜像。

## 调试连接与测量准备

15:42 按住 RESET 时，SWD DAP 可识别但 CPU halt 超时，原会话退出。
15:43:57 在 RESET 松开后重开 Commander，正确识别 CPUID `411FC272`、小端、
32 KB I/D cache；运行后读到有效 RTT 控制块 `0x24000000`。
此后保持同一个 Commander 会话完成换烧、系统复位及采集，不再退出后反复 attach。

采集走 Commander 的本机 `127.0.0.1:19021` RTT 通道，不开第二个探针连接。
先建立采集连接，再复位/运行以取得完整启动标记；标记不匹配或超时就不启动 host 测试。
配对测量时 SWD 为 1 MHz，显式禁止 RTT stop-mode；没有在测量窗口中 halt、读寄存器或擦写。
接口依据 [SEGGER RTT Telnet](https://kb.segger.com/J-Link_RTT_TELNET_Channel) 与
[调试命令说明](https://kb.segger.com/J-Link_Command_Strings)。

首次采集只拿到 Telnet 问候而缺少启动标记，准备检查失败，**未运行 host 性能测试**。
随后在连接保持期间复位，采集得到启动标记和每个窗口全部最终记录。
这些连接准备失败与协议功能失败分开记录。

## 发现的问题及反向验证

旧 HIL 每个 tick 直接发送 ArmStatus，并在同一 pass 紧接着发送 ArmDiagnostics。
USB 采用异步发送：前一帧仍占用发送槽时，后一 sender 的 payload claim 返回 busy；
旧 HIL 不保留该诊断快照。正式 Willow 早已通过调度器处理这个场景，HIL 却没有跟进。

旧固件的有效配对记录 `paired-live-*` 中，三个窗口主机诊断回调均为零，设备每个
20 秒周期记录均为 `dok=0,derr=40`，但当时旧验收条件仍给出了性能 PASS。
这是测试覆盖缺口，不能当成产品诊断通道正常。

增加检查后，先对旧固件执行 1000 次回显并要求诊断：回显无错配、无 gap、性能健康，
但 `diagnostics_callbacks=0`，**正确性 FAIL、退出码 2**。此反向检查证明新门槛能抓到问题。
修正版随后在每个正式长窗口收到 40 条诊断消息。

软件门槛：Linux host HIL 与 Windows MSVC 构建通过；native_sim/native/64 和
Cortex-M3 QEMU 共 2/2 配置、40/40 用例通过，无警告。测试断言异步 busy 后保留诊断、
状态最新值替换、发送公平性、成功/busy 计数以及 discard 不发送残留快照。

## 配对 500 Hz 性能

```text
willow_wirelink_hil_host --serial 323738373233511200260036 \
  --period-us 2000 --warmup 1000 --warmup-idle-ms 600 --samples 10000 \
  --cycles 3 --reopen-delay-ms 100 --strict-performance \
  --max-cpu-percent 25 --require-diagnostics
```

原有 rate、gap、尾延迟、生命周期及 CPU 门槛全部保留，另加诊断接收要求。
这三个窗口合计 30000 次命令—遥测回显，不是 30000 个 RPC。

| 窗口 | RTT p50 / p99 / max（ms） | Hz | Host CPU | gap | 诊断回调 | 结果 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 0 | 1.987 / 2.128 / 2.451 | 499.96 | 7.73% | 0 | 40 | PASS |
| 1 | 1.986 / 2.124 / 2.388 | 499.98 | 8.44% | 0 | 40 | PASS |
| 2 | 1.985 / 2.141 / 2.375 | 499.97 | 8.28% | 0 | 40 | PASS |

无 payload mismatch、RX drop、TX failure、dispatch/poll/service error；每窗口
8 个 RPC 开始、8 个释放，cleanup 成功。最慢 open / close 为 40.536 / 0.863 ms。
实际出现 2007 次 host RX ring-tail pause，不等同于丢字节或 Wirelink RX overflow。

板端对应窗口为 3 / 5 / 7，每个最终记录组完整，`miss`、owner deadline miss、
USB error、feed overflow、dispatch/completion error、invalid、lease expiry、operation
timeout 和调度硬错误均为零。

| 区域 | 平均 cycles / 调用 | 平均时间 | 最大 cycles |
| --- | --- | --- | ---: |
| USB OUT callback | 1266.57–1267.30 | 2.303–2.304 us | 1617 |
| USB IN submit | 2441.07–2445.77 | 4.438–4.447 us | 2896 |
| 协议 service | 5153.95–5170.34 | 9.371–9.401 us | 39069 |
| 遥测调度 service | 4675.80–4718.18 | 8.501–8.579 us | 14548 |
| 整个 owner pass | 11585.40–11605.59 | 21.064–21.101 us | 40788 |

owner 周期总量按最终窗口墙上时间归一化为 **3.116–3.138%**。这包含 IRQ 抢占和
500 ms 收尾 idle，是插桩区间占比，**不能叫纯线程 CPU 利用率**；service 是 owner
的子区间，不能叠加求和。v2 把编码移入 telemetry service，不能直接比较 v1 的
`status/diag` 与 v2 的 `status_publish/diagnostics_publish`。

在约 20 秒的最后周期快照，三窗口状态替换均为零，诊断提交均为 40。
最终快照却记录状态替换 74 / 75 / 247：主要位于 host 已关闭接收后的收尾阶段。
前两次 reopen 的下一轮 RAM 冒烟还让前一窗口命令数变为 10001；第三轮为 10000。
因此不能把 final 的 busy/replacement 总数当成正式测量窗口的缺帧数。

## USB 断开、重枚举与 RPC 恢复

先对首轮镜像、再对修正版各执行一次设备端断开：启动后 5 秒 disable USB，1 秒后
reenable，模拟实际 USB 消失/重枚举，而不是仅关闭 host session。两版均捕获设备
`detach/reattach` 标记，以及 Astrial 的恰好一次 disconnect / reconnect callback，RX 重新挂载。
此测试不等于断电上电或线缆电气故障测试。

修正版 Astrial open / stop / close 为 12.397 / 0.147 / 0.671 ms。
注入过程记录各一次 cancelled、disconnected、stalled read；`libusb_errors=2` 是该故障
路径下已检查的断开/STALL，不能写成“全部底层错误为零”。恢复后产品程序重新打开，
设备信息/设置、租约、模式 RPC 及 1000 次唯一回显通过，无 gap/错配，收到 4 条诊断，
cleanup 成功。最后已烧回普通修正版 HIL，禁用一次性重枚举。

## 无探针最终回归

16:02:27–16:05:09：关闭 Commander 和采集程序，检查没有调试进程后，依次运行修正版
对抗性会话重连、100 次产品生命周期、三个相同参数的长窗口，全部退出码 0。
没有修改严格性能门槛或加入打开重试。

- 对抗性会话重连 2/2 通过，相同 RPC operation seed、不同 session/租约，
  新会话探测完成；200 次唯一回显无错配/缺帧，最慢 open / close 32.286 / 0.629 ms。
- 产品生命周期 100/100 通过，10000 次唯一回显无错配/缺帧、cleanup 全部成功；
  最慢 open / close 43.351 / 1.211 ms，最差 p99 2.283 ms。
  约 200 ms 短窗口的最差 CPU 39.05% 受 Windows 计时粒度影响，不作为长窗口 CPU 结论。

| 无探针窗口 | RTT p50 / p99 / max（ms） | Hz | Host CPU | gap | 诊断回调 | 结果 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 0 | 1.984 / 2.088 / 2.382 | 499.97 | 7.58% | 0 | 40 | PASS |
| 1 | 1.985 / 2.092 / 6.479 | 499.89 | 8.12% | 0 | 40 | PASS |
| 2 | 1.985 / 2.092 / 2.228 | 499.99 | 8.91% | 0 | 40 | PASS |

三个窗口 30000 次唯一回显无错配、无缺帧，每窗口 8 个 RPC 开始/释放、cleanup 成功，
runtime storage 仍为 4024 B；RX drop、TX failure、dispatch/poll/service error 均为零。
host RX ring-tail pause 共 1957 次，正确性不受影响。最后核对无测试/调试进程残留，
板上保持普通修正版 HIL；不需要用户再次按 RESET。

配对采集与无探针测量是两组独立结果，合计 60000 次正式回显、240 条诊断。
本轮对固定快照的功能/性能门槛验收完成，以下历史事件与长期测量改进仍明确保留。

## 保留的开放问题

- 首轮第 50 次 USB 打开失败没有具体底层错误，之后诊断重跑未复现；不声称其根因已修复。
  已增加错误类别/数值/文本，后续长稳若复现再定位，不加入无差别自动重试隐藏失败。
- 首轮窗口有超标 gap；旧有效配对数据也显示少量发送 busy，但没有同步捕获到同一个
  超标事件，因此不将所有早期 gap 归因到同一个已修复原因。新快照按固定门槛独立验收。
- 若要更精确比较固件 CPU，应提供主机控制的测量窗口起止或独立线程 CPU 计数，排除
  收尾阶段；不能从当前 owner 墙上周期比值推断纯 CPU 占用。

## 原始证据

原始日志在 Windows `C:\Users\moonf\codings\wirelink-rpc-h3-20260907` 与本地
`build/rpc-h3-20260907/`。哈希针对原始字节：Python 采集为 UTF-8/原始 RTT 字节，
PowerShell 管道多为 UTF-16LE，不转换后再核对。

| 日志 | SHA-256 |
| --- | --- |
| `persistent-after-release.log`（完整调试/换烧会话） | `bf1971aefa961f18fd8a47b2ed5d9e01d1b5781690ca5b1f1758dd728abfb6d5` |
| `paired-live-device.log`（旧 HIL） | `8a7411b325660a5d8b406be1667f8340545cea911be8bb6b30d6e9133ff7f041` |
| `paired-live-host.log`（旧 HIL） | `44dc95f57801472fac6fa7f86b3dabb3340cfaa28c7aa945dc6c5d93c2748b45` |
| `diagnostics-negative-control.log` | `1d2aba1719d1fd130a0e8e1b47fd2e19350c5c3f46bdc3114178282debe742a6` |
| `scheduler-paired-device.log` | `a110d067ca6194bde68f71554ad849853399b24dcf4a688b01d43ab8183ed44a` |
| `scheduler-paired-host.log` | `953388a818e1037ea1a10b969dc767dcaaf5a16dacf26b8ee4351d0cb6e8b887` |
| `scheduler-reenumeration-device.log` | `ef22f4a3a06291ed7dfd988d9cc5ecaeb57031e73012b2191fa1320cdc93dc0e` |
| `scheduler-reenumeration-host.log` | `a1dea51f89124728e75b6907e3d7e6433b0ee6c882fe81dac2f7c43054fb9ea5` |
| `scheduler-reenumeration-recovery.log` | `2902e9adc78ee7420744f24c36ec3cd2f555043c62f0980ab6fc07c91d74e732` |
| `scheduler-adversarial.log` | `c3e30d5497facb31adbafabc72a613091a1386ffc0f0901c478c9faae30f23dd` |
| `scheduler-lifecycle.log` | `ae43345fc5c1db316cda6517ae70d04a60696d7c791b9ae8f9d5c5bb339efd2a` |
| `scheduler-clean-performance.log` | `0e453b37d5de6ffcef91263ddf1e4396339055ce69f4bfe9aa2042a2b2a373b0` |
