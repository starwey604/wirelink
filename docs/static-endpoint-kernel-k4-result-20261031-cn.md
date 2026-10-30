# K4 完整 USB FS 对照结果

18/18 新采集及独立原始算术重放通过，允许进入 K5。主要指标稳定改善超过约 40% 目标；可判定保护项没有重复失败或单次越界，**14 项保护观察仍不可判定，不能说全部保护通过**。这是隔离原型的端到端证据，尚非 K5 后默认产品结果。

输入和环境重登记见 [预登记](static-endpoint-kernel-k4-registration-20261031-cn.md)。证据在 `/home/ww/codings/wirelink-static-kernel-k3k4-20261031/k4-environment-rebase/`：`k4-registration.json`、18 组 `captures/k4-r*/`、`k4-status.json`、`k4-analysis.json`、`k4-replay.json`、完整逐项表 `k4-report.md`。CPU-off/on 分开，每轮 B/A、B/C 配对，再取三个配对比的中位数，没有合并 raw samples。

| 指标 | A 三轮 | B 三轮 | 配对中位改善 |
| --- | --- | --- | --- |
| RPC-off p50，µs | 1190.965 / 1197.024 / 1214.923 | 380.122 / 407.080 / 380.845 | 68.083% |
| MCU-on RPC active，µs/call | 1285.669 / 1310.456 / 1299.500 | 283.673 / 291.478 / 284.019 | 77.936% |
| MCU-on Sample active，µs/frame | 397.156 / 397.806 / 390.932 | 126.464 / 126.464 / 126.469 | 68.158% |

RPC-off p99 配对中位改善 59.116%；host RPC 全线程 CPU off/on 分别改善 22.171%／27.644%。host idle 为 A-off 0.189–0.205%、B-off 0.164–0.179%，A-on 0.167–0.186%、B-on 0.170–0.179%；MCU-on idle 为 A 0.03470–0.03494%、B 0.01313–0.01321%，均未触发 0.5 pp 保护。

不可判定来自 A 三轮波动超过登记阈值：off 的低频 p99、host Sample CPU、512/1024 B 双向流各分量；on 的 host 低频 CPU、host Sample CPU、Sample API p50/p99、512/1024 B 双向流各分量。原始三轮和单次比值全部保留，不重采或按平均收益将这些项改判通过。

Sample API-off p50 为 A 147.25–150.675 µs、B 6.067 µs；on 的 A p50 为 140.513–151.704 µs、B 2.317 µs，B-on p99 7.913 µs。on 的 A 不稳定，不能据此给 API 保护通过结论；B 的 on/off 差异也没有被归因为计时问题或缓存原因。新 API 将物理提交移到 owner step，因此 API 时间本身不能解释为纯 codec 收益。独立全 MCU active 成本如上；所有 A/B/C 的 on 组 Sample USB IRQ 均为 1002，数量相同没有消除执行路径、布局／缓存和 observer 交互问题。这里同时改变了已授权的语义和执行结构，并迁移 host，不能当作单独编译期选择器的因果估计。

libcsp 的 CPU-off RTT p50/p99 成本比为 B/C 0.916／0.920，on 为 0.817／0.798；MCU-on RPC／Sample active 比为 1.003／0.970，host RPC CPU off/on 为 0.595／0.571，相关可判定项满足成本≤1.2。部分低频／双向大包项目因 C 波动不可判定。明确未达 goodput≥C/1.2 的项：32 B off 单向 H2D 比 0.830、off 双向 D2H 0.617、on 单向 D2H 0.810、on 双向 D2H 0.663。MCU idle B/C 约 1.475 倍，绝对差约 0.0043 pp，也不能称所有 CPU 指标追平。跨库目标未全面达到，按登记不构成 K5 硬停止条件。

完整 stream 表按每个尺寸、方向、observer 和分量给出，不使用双向总量掩盖一条方向；窗口 CPU、全阶段 host CPU、MCU active、drain 和完整字节账本在 analysis 的 `extra`。吞吐变化时绝对 stream CPU 不是等工作量成本，没有伪造 MCU 每字节窗口账本。

资源审计：A/B 的 `data+noinit+bss` off 为 57224／49984 B、on 为 57760／50520 B，均减少 7240 B；`.text` 减少 20256 B，IRAM text 完全相同，没有搬 core 到 IRAM。Xtensa 新 endpoint 4408 B、port 88 B、USB adapter 152 B。C 的对应静态 RAM off/on 为 151952／152536 B，包含其原有 64 KiB heap、CSP buffers 和额外 route worker，未为对照缩减它们。资源、每线程 stack_unused、allocator 峰值、host RSS 都保留；第一轮 on 的 main stack_unused 为 A 4240、B 4912、C 5152 B（8192 B 栈），RSS 为 A/B 30640、C 32672 KiB，不能将单次观测包装成统一峰值。

工具负例拒绝缺样本、过短 pacing、单 slot、损坏 stream 账本、缺少大包、短窗口，并保留极端 RTT。独立重放首次误要求 libcsp 含 Wirelink 专属 `core_profile`，失败日志保留，修复独立工具后 r2 全部通过，未改采集器、数据或登记。最初 nice 漂移尝试没有放行 warmup；重新冻结实际 nice 16 的完整块没有再发生环境漂移，未改任何调度设置。

K4 后板卡保留新 B-on：`static-kernel-k4-20261031-B-o1`，bin SHA-256 `2f01da36c32958873216ff98bd0ea27057b25989a1438ebb05180fee70f19771`，native serial `10003B6DEC94`，FS/MPS64。`k4-final-B-smoke` 通过，`k4-handoff.json` 核对 bridge 与 USB 无打开句柄；没有恢复旧 A 或历史 COBS/UART 固件。K5 最终源码仍需重新构建、确认并交接。

K3/K4 证据索引：上级目录 `k3-k4-evidence-index.json`，SHA-256 `165ef78c765d267be13596354248ed5b270ccea9385c3c58044275ad09fd76e4`。封存 15531 个文件、7228 个符号链接，含失败尝试、源码 archives、生成物、最终镜像／map／config／编译链接输入、软件报告、raw captures 和分析工具；不含对象／静态 archive 中间文件、依赖缓存及 VCS 元数据。另校验外部六组设备／host 与旧 K2 索引（`d9b0a0ef2b72df6c6d455aa873d6225dc546a0eba66ba0cace6077fc51cbef8a`）。K5 起使用新证据目录，此索引不继续追加。
