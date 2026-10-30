# 执行与等待（开发生成契约 0.12）

核心、WLC 生成代码、adapter 和消费者需要一起重编译。本轮修改执行接口与布局，
保留 frozen protocol-v1 向量和 codec 字节。英文镜像见
[execution-contract.md](execution-contract.md)。

## 单 owner 的有界 pass

`wl_readiness_t.runnable` 表示现在能执行一次有界状态变化；`blocked` 表示任务在等
输入、资源或时间；`next_deadline_ms` 是相对等待时间，`WL_NO_DEADLINE` 表示无计时。
零 deadline 为 runnable；合并使用标志 OR 和最小 deadline。查询不提交、不取消、
不处理 expiry，也不调用业务回调。

`wl_pump_result_t.did_work` 只统计上一 pass 已做的工作，不要求继续循环；`polls`
和 `services` 记录实际调用次数，包括未产生业务事件的协议处理。完整 RX 帧、已发布
completion、可提交队列项、待通知终态为 runnable；BUSY 后的非空队列为 blocked。

owner 执行 step、查询合并 readiness，只在无 runnable 时等待，并按最近业务计时
（发布、停止等）限制等待。每个 pass 在 adapter completion 前采样一次时钟，内部
沿用同一快照。外部查询重新采样；手动推进时钟必须唤醒 waiter。生产者不修改 RPC
或协议状态。service/application step 必须配完整 readiness；pump 分别门控它们。
手动有界 poll 保留给仿真或真实主动 polling 输入，旧 executor 无条件扫描路径已删除。

## 静态 service

`wl_endpoint_set_services` 借用不可变数组（最多 64 项）。有 step 的 service 初次
激活，以后仅在 active 或真实 timer 到期时执行。返回 `wl_work_result_t`，分别报告
本轮 did_work 与后续 runnable。新增 owner 业务调用 `wl_endpoint_activate_service`
或 `wl_endpoint_activate_policy`，同时通知 waiter；跨线程先通过 executor/同步队列
发布任务，不能直接并发操作核心。

RX/终态事件和 core 资源 generation 变化激活 service。TX consumed/drained/writable、
终态 take、RX release、claim abort 更新 generation；它是 owner 字段，不由 ISR 写入。
deadline 查询仍有有界 service 数组扫描，本轮没有引入 timer heap。

## 通知和关闭

先发布再通知；检查与 wait 之间也不能丢通知。IRQ/DMA 使用容量为一的 semaphore。
service 在读取发布状态前消费既有锁存；之后到达的工作留有新锁存，或被随后 core poll
观察。host 保留 generation/coalescing event。UDP 同时查询 socket 的电平状态，保证
持续 TX、不进入 wait 时仍能接收；RX slot 满时屏蔽 socket RX。

IRQ 每个有效 callback 最多通知一次：RX commit、错误、pause 和 TX completion 仍立即
发布，通知在有界的 32 次循环结束后、释放 binding 锁之前发出。所有有效退出路径汇合
到该通知点；零进展且无发布时不通知。没有跨 callback 的 pending 标记，也不读取
semaphore 计数决定是否 give；新 callback 总能留下新的锁存。附加 notifier 同样按批次
调用，不能把通知次数解释成帧数或业务事件数。

确认只能发生在所覆盖状态的观察之前。wait 消费一次锁存后必须重新推进／查询；不得在
readiness 检查之后 reset 锁存。RX-only pass 可以跳过 adapter service，此时旧通知最多
造成一次立即返回；不能因为没有完整帧而重新锁存或无限空跑。若外部队列共用 waiter，
它的状态也须由 owner 的等待前检查覆盖，或像 host 一样用 generation 覆盖队列派发到
wait 的窗口。先同步发布任务，再 notify；不能跨线程写 RPC 或 endpoint。

`wl_endpoint_get_readiness_at(endpoint, sampled_at_ms, &hint)` 使用调用者在 endpoint
时钟域取得的快照，查询本身不采样时钟、不处理到期工作。进入 wait 前用
`wl_wait_remaining(hint.next_deadline_ms, sampled_at_ms, wait_now_ms)` 扣除中间耗时，
并与调用者业务期限取最小值。零值直接继续 owner pass；无穷 sentinel 保持无穷，
其余值保持有限。两次采样之间须少于一次完整的 32 位时钟回绕。原自动采样的
`wl_endpoint_get_readiness` 仍适合立即消费查询结果的调用者。

Zephyr raw owner 可在查询前保存 `k_uptime_ticks()`，在等待前调用
`wl_zephyr_timeout_since(maximum, sampled_ticks)`。它将相对 kernel timeout
绑定到该快照，保留绝对 deadline、零等待和无穷等待；64 位 timeout 内核直接返回
绝对期限，避免函数返回后再增加一份相对预算。以微秒表示业务上限时使用
`wl_zephyr_timeout_us_since`，其中 `UINT32_MAX` 表示无穷，有限值先以 64 位
换算 tick。两者声明在 `<wirelink/zephyr/timeout.h>`，换算实现位于平台执行层，
不在调用者内联展开，也不消费通知或推进协议。32 位 timeout 内核扣除已耗 tick，
要求两次取样间隔小于一次完整 tick 回绕；内核调度与 tick 舍入仍适用。

Zephyr 的毫秒 timeout 先以 64 位换算 tick，再收窄；32 位 timeout 内核对超长有限值
提前唤醒并由 owner 重算，绝不将其转换成 `K_FOREVER`。允许内核 tick 取整及正常调度
延迟，不延长 RPC 的逻辑期限。DMA 公共 wait 在 stop 前后检查取消；quiesce 则继续
等待真实 buffer 归还和 callback 退出，取消业务等待不代表内存已经归还。

IRQ 的附加 notifier 必须 ISR-safe、无阻塞且不能重入 adapter；Zephyr UDP 仅线程
通知，Astrial 回调在 I/O 线程执行。通知上下文必须活到 quiesce 完成，外部 producer
线程退出后才能销毁。

IRQ 使用静态 generation binding；stop 与 ISR 在解引用 adapter 前使用同一锁，移除
绑定后迟到回调不访问旧存储。DMA 还统计正在执行的回调；quiesce 等待驱动归还 TX/RX
内存并等待这些回调结束，再解绑 notifier。若驱动始终不归还 abort buffer，不能仅靠
超时认定关闭完成或释放它。

## TX 内存与线路

`wl_tx_consumed(ctx, token, result)` 结束 adapter 对源内存的借用，FIFO copy 与
DMA DONE/ABORT 使用此边界。它不等于 peer 接收、ACK 或 RPC 成功；可靠事务仍保留
编码图像直到逻辑结果被 take。重复或过期 token 返回 NOT_FOUND。

`WL_SINK_SENT` 表示同步结束内存访问；配置了 physical drain 时，它还保证该可观测
边界已经结束。无法同步提供此保证的 sink 必须返回 `WL_SINK_STARTED`，再由 owner
分别报告 consumed 和 drained。

仅在确实需要物理 idle 时，于发送前设置 `wl_set_tx_drain_required`，并独立报告
`wl_tx_drained`。drain 门控新的物理提交和可靠 ACK timer；timer 从观察 drain 的
owner pass 开始，不猜测线路完成时间。先到 ACK/响应可以完成逻辑工作，但内存释放
和物理串行化仍独立执行。

IRQ 最后一次 FIFO copy 后关闭 TX-empty，不在 ISR 等待 idle。可选 idle fallback
只在 drain pending 时按 `drain_check_ms`（默认 1 ms）查询；DMA 同样有界。能力缺失
或负返回使 drain 失败，CDC 不伪造 line idle。BUSY 需明确 writable 事件；DMA/UDP
在驱动没有 writable 通知时用真实 retry deadline 恢复。

## RX 借用和缓存

`WL_PUMP_EVENT_CONSUMED` 表示回调已经 release/take；`LEASED` 表示 owner 保留 RX
事件，须保存 event，在业务接收完成后 release，并在 close 前归还。持有期间 core
不重复解析 RX。`UNHANDLED` 由 pump 默认清理。

delimiter cache 仅 consumer 写入：记录某个 read cursor 下已找到的 delimiter 或
已扫描的无 delimiter 前缀。consume 失效；新的 acquire write cursor 只扩展未扫描
后缀。producer 以 release 发布 cursor，不访问缓存；overflow 独立锁存，无完整帧
时仍能处理。

生成 runtime 仅在 timer 到期时处理 expiry，按对应 delivery 资源提交 response；
async 终态通知和 retiring handle 分别计算 readiness。同步 response 提交失败后保存
owner 资源 generation，generation 不变时保持 blocked；重复查询或手动 service 不会
重试同一次错误。首次失败尝试属于 did_work 历史。RPC deadline 查询仍扫描有界
槽且无副作用；本轮没有引入四槽索引，也没有修改 replay 或到期响应接受规则。

实测、失败项和验收状态见[执行报告](execution-refactor-round-1-result-cn.md)。
