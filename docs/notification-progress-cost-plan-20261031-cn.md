# 当前内核的通知与推进成本研究

日期：2026-10-31。[接受的基线](notification-progress-baseline-20261031-cn.md)为 A_current。本轮完成入口清理和首轮账本，尚未实施产品通知优化，也没有新的硬件性能比较。原有 Sample CPU-off/on 差异仍不能归因于计时、缓存或某个通知点。

## 已完成的入口收敛

cross-benchmark 提交 `f2896f7` 移除旧 Wirelink pair、UART/USB 执行体、旧 runtime profile 及其专用执行测试；旧命令保留明确失败诊断。历史数据读取器和其他库的比较入口保留。当前 Wirelink 仅从 `usb/static_kernel/` 构建和按注册执行，capture 不再允许裸 runner 绕过 build 身份检查。逐项边界见该仓库 `retired-execution-entries-20261031.csv`。

实际新 host 和 ESP32-S3 CPU-off device 构建通过。五个核心／USB 对象的 `.text` 和 `.literal` 与接受的 off 固件逐字节相同；该结果仅支持本次入口清理没有改变所查机器码。临时构建未刷板，板上仍为已接受 B-on。

## N0：通知链及计数边界

`src/packet_port.c` 的 activity 发布使用 release store，并无条件调用 notify；消费侧 exchange 清除 activity。RX commit/release、TX publish 均可能进入该链。不能从 activity 已置位推断再次 notify 没有同步或生命周期作用。

Zephyr USB 的正常接收完成依次产生 RX commit 通知、RX_REARM activity 通知，最后在 callback 的最后一次 adapter 访问之后递减 callbacks_active 并直接外部 wake；owner release RX 还有一次 activity 通知。正常发送完成通过 DRAINED publish 通知，再由 callback 尾部直接 wake。按普通单请求／单响应的静态路径，可以列出六个外部通知机会。这不是实测每 RPC 通知数，也不是每 IRQ 六次唤醒；错误、零长、重挂和停止路径还需单独统计。尾部 wake 参与停止确认，不能依据计数将它删除。

设备外层为上限 1 的 semaphore；host 外层为 Astrial CoalescingEvent。两侧均在 owner step 前后 service adapter。应区分数据发布、通知函数调用、锁存合并、ready 检查、实际 owner step、wait 尝试和真正阻塞／返回，不能以其中一项替代其他项。

## N1：使用真实内核的非计时模型

新增默认关闭的 `WIRELINK_BUILD_NOTIFICATION_CENSUS`，源码及使用方法见 `benchmarks/notification_progress/`。使用真实 profile-v2 生成代码、C-owned storage、两侧交替 RPC、RX lease、物理 TX token 和关闭流程。每行完成 32 RPC、64 普通 Sample 消息，双向合计发送／接收 128 包；接纳后输入 poison，延迟 lease 内容校验，检查一次回调／副作用和关闭清空。

每行包括两侧合计计数。port notify 与 callback 尾部直接外部 wake 分开；notify 总计为二者之和。

| 完成模型 | budget | port notify | 直接 wake | owner step | 锁存已置位时的信号 |
| --- | ---: | ---: | ---: | ---: | ---: |
| inline SENT | 1 | 384 | 0 | 448 | 96 |
| inline SENT | 16 | 384 | 0 | 176 | 240 |
| async DRAINED | 1 | 384 | 0 | 448 | 128 |
| async DRAINED | 16 | 384 | 0 | 256 | 192 |
| split CONSUMED/DRAINED | 1 | 512 | 0 | 576 | 128 |
| split CONSUMED/DRAINED | 16 | 512 | 0 | 320 | 256 |
| USB class 普通回调模型 | 1 | 512 | 256 | 448 | 512 |
| USB class 普通回调模型 | 16 | 512 | 256 | 256 | 576 |

这组工作量里 admission/capacity notify 和 submit_busy 均为零；不能代替压力测试。模型没有运行 USBD、系统 semaphore 或时钟，也未覆盖所有错误／停止回调交错。锁存合并与 signalled-not-ready 数值取决于显式软件调度器，不是设备 owner pass 或 OS 唤醒数。USB 模型增加通知调用而没有同比增加 step，说明“减少通知调用”与“减少推进次数”必须分别验证；模型没有提供可预测的性能提升比例。

Release 和 Clang ASan/UBSan 各 29/29 CTest 通过。外部入口 Python 检查 37/37、独立 framing 1/1、当前构建身份检查 2/2 通过。原始 JSON、检查日志和机器码对照位于 `/home/ww/codings/wirelink-notification-progress-20261031/`。

## 已接受原始 CPU 数据的再分解

`accepted_cpu.py` 按原索引逐项验证三个 B-on 原始记录、完整采集 manifest、注册身份及阶段分母。以下是各阶段每单位微秒的三次中位数，240 MHz；RPC 分母 10000，Sample 分母 1000。

| 计数项 | RPC µs/call | Sample µs/message |
| --- | ---: | ---: |
| active | 413.663 | 158.645 |
| owner inclusive IRQ | 223.838 | 86.047 |
| USBD inclusive IRQ | 56.212 | 19.184 |
| UDC inclusive IRQ | 72.397 | 25.886 |
| library worker inclusive IRQ | 0 | 0 |
| USB IRQ span inclusive | 49.873 | 9.807 |

三个 RPC 阶段均为 20002 USB IRQ，三个 Sample 阶段均为 1002。线程／IRQ 计数存在重叠或不同统计范围，禁止加减得到 exclusive 的“框架成本”。这只是当前 observer-on 账本，没有硬件 notify/step/wait 次数，不能据此断言某项是主要可消除成本。当前脚本只重验所用保留原始记录，不宣称删除旧产物后仍可完整重放旧索引。

## N2：下一轮实际计数与验证

下一步先做单独的诊断构建，把 producer 与 owner 分开记录 publication、notify（原因包括 RX commit/rearm/release、TX consumed/drained、fault/stop、callback exit）、ready 查询、step budget／实际工作量／空转、wait 进入／返回及超时。host 同步记录 event 合并和事件循环推进；设备同步记录 semaphore 及 adapter service。计数器不得读取即清除产品状态，不在热路径打印，不用软件模型替代 USB 栈；计数读取放在阶段边界，并声明并发／溢出处理。

诊断构建与未插桩 A_current 分开，先验证 observer 本身对 CPU-off/on 和 Sample 路径的扰动。只有能关联“发布 → 通知 → ready → 推进／等待”的实际账本之后，才选择一个候选变化。首个候选不得同时改变缓存、布局、inline、调度优先级、时钟或工作量；当前两套固件／gated host 保持参照身份。

任何合并通知候选先过 lost-wake、deadline-before-RX、budget1–16 公平性、队列压力、generation 快照、source lease、CONSUMED/DRAINED 分离以及 callback stop/join 回归。尤其覆盖 ready 检查与 wait 之间的发布、最后 callback 退出与停止确认交错。再预登记同环境、同 observer、同资源的 A_current/B 比较，环境仍须使用已修复的逐线程 nice／IRQ 契约并重新核验。未知项保持未知，不凭相同 USB IRQ 数给整个编译期专门化方向定性。
