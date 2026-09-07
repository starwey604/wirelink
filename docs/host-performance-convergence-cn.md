# Host 性能收敛：空队列免锁检查与有界唤醒

2026-09-07，承接 [ABI 26 实板记录](product-session-hil-cn.md)。本轮修改 desktop
executor / Astrial USB adapter；C11 core、WLC、生成 ABI 26 和线上帧格式不变。
`Executor` 的 C++ 对象布局变化，消费者须重新编译；不发布、不合 main、不替换长期测试快照。

## 当前结论

默认关闭插桩的最终 host 构建：无探针复测 3/3、随后 RTT 板端观测 3/3 严格窗口通过，
分别记录 30000 次正确回显、零 gap。主机 CPU 的相邻对照均值下降约 11%；
H7 owner pass 平均约 21 µs，**不是整颗 MCU 的 CPU 占用**。历史长尾尚未捕获归因。
本页实验时这些 host 改动尚在工作树；现已提交并同步产品 pin，最新验证见
[集成收尾](dev-closeout-cn.md)，收尾前条件见 [合并评估](dev-main-merge-assessment-cn.md)。
本页下文保留原始对照细节和失败记录。

## 本轮改动

- **RPC 空路径**：此前每个 owner pass 都加锁扫描 8 个代理槽位，即使该 endpoint
  根本不用代理 RPC。现在先读原子待办标记；有新请求才在一次临界区内收集工作，提交和
  completion 在锁外执行。栈上 job 的存活期、队列上限、停机完成协议保持不变。
- **LATEST 空路径**：原子标记发布队列是否非空，空时不加锁、不复制、不清零 512 B
  临时缓冲。实际提交、acquire/complete 仍互斥，保留多生产者、按 message ID 合并、
  backpressure 重试和公平轮转。更新标记与修改队列在同一锁下，不会覆盖新生产者的待办。
- **有界通知**：executor 和 USB adapter 使用一个可合并事件，最多保留一个 permit。
  不再按每次 RX/TX 累积计数，也不需要批量排空历史通知。USB 只用外部 callback、
  从不调用 `wait_for_activity()` 时，同样不会无限积累未消费通知。
- **诊断开关**：`WIRELINK_HOST_PROFILING=ON` 增加分段墙时、Linux 线程 CPU 时间、
  最大值和慢样本。默认 OFF，不增加热路径读钟或日志输出。

这里的“免锁”是**空路径的原子检查**，不是把 MPSC 数据结构宣称为完整 lock-free。
事件内部仍通过平台 semaphore 挂起/唤醒线程，不使用忙等自旋替代休眠。消费 permit 后
才清除 pending，并 acquire 已合并的发布；executor 的 generation 检查仍负责覆盖
处理过程到睡眠之间的新活动。

## 哪些锁没有删

| 区域 | 决定及原因 |
| --- | --- |
| C core RX / USB claim-commit | 已是单生产者、单消费者；不再叠加并发机制 |
| Host LATEST 实际槽位写入 | 多业务线程可同时提交；不能当作 SPSC |
| Host RPC admission / completion | 保护等待者的栈对象和条件变量销毁时机 |
| libflorid 租约 / 业务结果 | 多字段一致状态，不用几个无关联原子读替代快照 |
| H7 `ArmProtocol::state_lock_` | 共享租约、统计、异步完成队列；32 位平台上的 64 位原子未必无锁 |
| Astrial 读生命周期 / disconnect 配置 | 冷路径 teardown 保证；本轮未修改 Astrial |

板端下一候选是量化 `state_lock_` 的持有时间，再区分 owner-only 字段、跨线程快照、
多生产者 completion。不能先用非原子 payload 的 seqlock 或可能阻塞的 64 位原子替换。
本轮没有改固件，不能声称降低了固件 CPU；最新同镜像测量见后文“H7 板端观测补充”。

## 插桩使用与归因边界

HIL 主机独立构建，指向本轮 Wirelink 源码，不改产品默认依赖 pin：

```sh
cmake -S firmware/tests/apps/willow_wirelink_hil/host \
  -B build/willow-hil-perf-profile -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_SOURCE_DIR=/path/to/wirelink \
  -DWLC_EXECUTABLE=/path/to/wlc -DWIRELINK_HOST_PROFILING=ON
cmake --build build/willow-hil-perf-profile --parallel 2
```

`wirelink_host_profile_v1` 记录 USB callback/submit、owner/pump/application、command
及其锁等待、notify、wait、业务 callback/wait。`rx_to_owner` 和 `notify_to_owner`
是首个待办通知到下次 owner pass 的**批次近似**，不是逐帧关联。
`usb_rx_gap` 是 callback 间隔，含线上/设备/主机调度等待，不代表 libusb 单次处理耗时。

计时使用独立诊断时钟，不改变协议的注入时钟或 timeout。各范围嵌套，不能相加。
普通 wait 包含正常睡眠；墙时大而 CPU 小只能说明等待/未运行，不能单凭此确定是谁阻塞。
非 Linux 输出 `cpu_clock=unavailable`，不把零当实测 CPU。

过程内最多记录前 2048 个 ≥4 ms 样本，超出的数量照常报告，累计统计继续更新。
热路径不分配、不打印；全部 worker 停止后才 reset/export。HIL 每轮统计包含握手、
warmup、idle、cleanup，而严格性能窗口只统计 10000 次回显，两者分母不同。
插桩自身有开销；最终 CPU/延迟验收使用 OFF 构建，不能混用。

## 实板对照

同一 Linux/GCC 16.2.1、同一 H723 550 MHz、同一普通 HIL 镜像；不接机械臂，
不修改 USB/固件参数、不启用 J-Link/RTT、不调进程优先级。每组 3 个窗口：

```sh
willow_wirelink_hil_host --vid 2fe3 --pid 574c \
  --serial 323738373233511200260036 --period-us 2000 \
  --warmup 1000 --warmup-idle-ms 600 --samples 10000 --cycles 3 \
  --reopen-delay-ms 100 --strict-performance --require-diagnostics
```

门槛不变：≥475 Hz，gap≤0.1%，p99≤4 ms，max≤20 ms，open/close≤500 ms。
原始日志保留在 `Ragtime_Firmwares/build/perf-round-*.log`，不覆盖此前失败记录。

| 构建 / 窗口 | CPU（一个核=100%） | RTT p99 / max（ms） | gap | 严格性能 |
| --- | ---: | --- | ---: | --- |
| 原基线 / 0 | 5.28% | 2.078 / 3.137 | 0 | PASS |
| 原基线 / 1 | 5.32% | 2.095 / 9.155 | 0.1895% | FAIL |
| 原基线 / 2 | 5.35% | 2.114 / 14.174 | 0.3085% | FAIL |
| 首次优化 / 0 | 4.89% | 2.183 / 4.073 | 0 | PASS |
| 首次优化 / 1 | 4.46% | 2.088 / 5.267 | 0.0100% | PASS |
| 首次优化 / 2 | 4.69% | 2.068 / 2.637 | 0 | PASS |

以上均协议正确、10000 次唯一回显完整。首次优化三窗进程 CPU 平均约下降 12%，
不是固件 CPU，也不是可以推广到所有机器的保证。对照复测结果见后续验收补充。

把旧可执行文件再跑一组时，三窗也都通过，CPU 5.30/5.16/5.29%，p99
2.077/2.081/2.094 ms，max 2.923/3.420/8.716 ms，gap 0/0/0.0699%。
因此不能仅凭“新版本全绿”认定旧版本长尾有确定的软件根因；CPU 的下降与锁计数的减少
比单次 max 更有证据。该组日志是 `perf-round-baseline-recheck.log`。

随后最终 OFF 构建的复测 (`perf-round-final.log`)：

| 窗口 | Hz | Host CPU | RTT p99 / max（ms） | gap | 结果 |
| --- | ---: | ---: | --- | ---: | --- |
| 0 | 500.02 | 4.97% | 2.067 / 2.879 | 0 | PASS |
| 1 | 500.02 | 4.67% | 2.066 / 2.745 | 0 | PASS |
| 2 | 500.01 | 4.34% | 2.075 / 2.799 | 0 | PASS |

三窗 CPU 均值 4.66%，相邻旧构建复测均值 5.25%，下降约 **11%**。open 最大
7.014 ms、close 最大 0.246 ms。新旧版本均保持功能正确；两组优化 OFF 合计
60000 次唯一回显、6/6 严格窗口通过，但不能据此结束历史偶发长尾的调查。

最终 OFF 二进制的生命周期验证：100/100 PASS，100 个默认 session 互异，open 最大
7.841 ms、close 最大 0.476 ms、回显 max 2.878 ms。每轮 10 warmup + 100 次测量，
这类短窗口不启用严格性能判定。对抗性重连 2/2 PASS：固定 session 7698/47016
产生相同首调用编号 3315986515，上一轮遗留租约/RPC 后，新租约和 probe 正常完成。
这是映射 FCI 的现有测试，不是托管 RPC v2 的完整旧响应隔离证明。

带插桩的优化前/后两组均 3/3 PASS；command lock 次数约 83151–83435 → 33003–33031，
减少约 60%。owner 线程 CPU 累计约 664–676 ms → 532–550 ms，但计入插桩本身，
只用于定位。**插桩窗口没有捕获此前几十毫秒的坏样本，不能宣称该历史长尾已根治。**

## H7 板端观测补充

随后使用同一最终 OFF host 和同一 HIL 镜像，重新上电连接 J-Link；
`SetAllowStopMode = 0`，通过同一个 Commander 采集 RTT，未重新烧录或调整固件参数。
日志有完整启动标记。3 个严格窗口均约 499.99 Hz，30000 次唯一回显、零 gap、
零 payload 错误；host CPU 4.36/4.66/4.64%，RTT p99 2.072/2.067/2.073 ms，
max 2.558/2.917/3.166 ms。open/close 最大 7.431/0.258 ms。
这组有探针的观测单独列出，不与前面的无探针对照混作同一批实验。

RTT 最终窗口 3/5/7 各有 12 类完整记录；H723 550 MHz，I/D-cache、LTO 开启：

| 测量范围 | 三窗平均时间范围 | 三窗记录的最大值 |
| --- | ---: | ---: |
| USB RX callback | 2.473–2.476 µs | 3.051 µs |
| USB TX submit | 4.292–4.300 µs | 5.189 µs |
| USB + 协议 service | 9.425–9.451 µs | 71.045 µs |
| 遥测 service | 8.467–8.546 µs | 35.182 µs |
| owner pass | 21.122–21.169 µs | 74.465 µs |

漏 tick、owner deadline miss、协议 feed overflow、dispatch/completion error、
无效请求、租约过期及业务超时均为零。500 Hz HIL 下板端处理有余量；
约 2 ms 的端到端 RTT 不能解释成固件执行了 2 ms。

测量边界：

- `k_cycle_get_64()` 测的是经过时间，含抢占；未启用 `THREAD_RUNTIME_STATS`。
  owner 累计计时 / 窗口墙时为 3.1250–3.1527%，不是 H7 总 CPU，也非纯 owner 线程 CPU。
- owner 从等待返回后开始，到快照与日志输出前结束；协议、遥测等范围嵌套，不能相加。
  USB adapter 的峰值是启动以来最大值；本次没有板端 p99。
- 每个控制 tick 不只对应一次 owner pass；USB 和定时通知也会唤醒 owner。
- 板端窗口包含约 500 ms 的收尾 idle，LATEST replacement / busy 包含租约结束后的收尾，
  与 host 正式测量窗的零 gap 不矛盾。
- HIL 不运行 CAN、电机、NVS 或看门狗。这不是完整 Willow 负载验收，
  也未验证 1 kHz、资源耗尽或板端锁持有时间。

下一轮如继续板端优化，应先补线程 CPU 和锁持有时间，再跑 1 kHz/并发负载；
当前数据不支持直接删掉 `state_lock_`。观测结束已退出 Commander/RTT。

## 软件验证

- Release：16/16（UDP 故障注入、生成 RPC executor、C/C++/Python bridge 等）。
- 并发专项：executor、coalescing event、RPC executor 各重复 50 次，150/150。
- ThreadSanitizer：同三项各重复 20 次，60/60，无竞态报告。
- ASan/UBSan：native 15/15；Python ctypes 装载需预加载 Clang ASan runtime，单独通过。
  首次未预加载产生 `undefined symbol: __asan_report_store4`，保留失败日志；Python
  检查关闭解释器泄漏检查，native 检查不因此关闭。不能把首次命令记成 16/16。
- Profile ON：6/6，含多线程采样、缓冲饱和、停机后导出及 reset。
- 安装包 C/C++20/Host 消费：3/3，包含新增 header 的安装检查。
- libflorid 指向本轮 core 的独立 Release 构建：4/4。
- HIL 主机 OFF 模式使用原 pinned core 的独立构建也通过；没有强迫产品依赖升级。
- CI 增加跨平台 host、profiling 和安装消费覆盖；本地未执行远端 Windows/macOS CI。

新测试覆盖百万通知合并、普通数据发布、4 producer 通知风暴、4 个 LATEST producer
的 40000 次更新与完整 payload 检查，及 producer 全部转入 idle 后最后一条更新仍送达。

## 可复核产物

构建在独立 `build/` 目录，不覆盖旧 binary 或产品默认 submodule / west pin。
Wirelink 本轮源码工作树基于 `4f2d98a`；libflorid `b2a21ce`，FCI `47d5323`，
WLC `c6b6a8f`。Ragtime 只改 HIL 的可选诊断接入，未改 `ArmProtocol` 或 Willow 固件。
HIL 不启用 profiling 时仍能使用原 pinned core。

| 产物（Ragtime `build/` 下） | SHA-256 |
| --- | --- |
| `willow-hil-host-abi26/willow_wirelink_hil_host`（原基线） | `cc81c07863cd67ef77f1f3b67acd0c3212887e509bcbff1651ef1fae901963fe` |
| `willow-hil-perf-release/willow_wirelink_hil_host`（最终 OFF） | `b6533cc00a2190cc27bbeeaf80e5b244d858a84f4ae9de95735757d4d3d849a1` |
| `willow-hil-session-abi26/zephyr/zephyr.elf`（全程未改） | `e665bb9e76c306b4ebe8ddb7c967b90c9bc55e1c99e4812713a07f643e69801b` |
| `perf-round-baseline.log` | `0d862fe90dc749e3025778a4e6e214f01554e35e41eab0730eeb1cbe881a03cc` |
| `perf-round-baseline-recheck.log` | `4fe3f23a1e77147eb53c3d5a5bc1900346de813cc6644e6ea820fbf33b04d17e` |
| `perf-round-after.log` | `c8f806bd00fed8c22a631caea9d0c401605f6fbf3fc851274474ba93be7b93c0` |
| `perf-round-final.log` | `1a15d9e469e810cc1ced4115bc8e8682b9a53c4c3381f6f026afcb0f18d34416` |
| `perf-round-profile-before.log` | `a12613a65fb342f293bbd071588557953fb9ed9b8637f21c811d609e7db47f51` |
| `perf-round-profile-after.log` | `5251ddc9a4edd8484f7fa7c613c909c5de2d73ad5a1320492d5b8c4ad8f149ff` |
| `perf-round-lifecycle.log` | `561a82b791984f32bab9a9c82589b53c0aba52acd87d1ba0171b1cfdceee5f53` |
| `perf-round-adversarial.log` | `ed6645c073c44ed891cf00877416eaff18d9e1cd136816552fcd2981de666a48` |
| `h7-performance-host.log` | `fbdc57e521575e9b37a251b9179248bc4d704b8e99603c057a98c5dce177f7aa` |
| `h7-performance-rtt.log` | `974a5895d60a24870f3c78cd5aedcc9e3f0ccd27f11761a8500249841f29b404` |
