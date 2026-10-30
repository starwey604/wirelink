# K4 完整 USB FS 对照预登记

正式采集前登记，原始证据目录：`/home/ww/codings/wirelink-static-kernel-k3k4-20261031`。机器可读登记 `k4-registration.json` SHA-256：`c65ad89e2f1420c915aa5aa17e70f1a253e4de26bab788a4a584d5e95a3601cb`。此文不宣称已得到性能结果。旧 K0–K2 封存目录保持只读。

A 使用封存的旧 device 和旧 host；B 使用共享静态内核的新 device／新 host；C 使用当前 libcsp 原有 project Bulk、codec 和业务。六组均已通过完整负载前的 smoke；A/B 另通过双向 63/64/65、127/128/129、511/512/513 B 编码边界资格检查。C 的 279 个普通源码文件与 K0 archive 一致；该 archive 漏存了 `zephyr -> contrib/zephyr` 目录符号链接，已在新研究副本中恢复，未改外部 vendor。C 独有路由线程计入全进程 CPU。

B 采集源码在根仓 `23dc8c0` 冻结，WLC `af819f7`，benchmark `27d517b`；后续根仓 `0c0426d` 只修复未链接进该 native USB 镜像的可选 stream RX 上界。登记包含源码、生成器、ELF/bin/map/config、编译和链接输入、六份 host、工具及线程／系统环境哈希。结构审计确认 B 实际链接 `wl_kernel_step`／packet port，没有旧 endpoint／pump／RPC／通用 frame 编解码执行符号。A/B 配置差异限于旧 Wirelink 模块及其子选项的禁用、新 identity；CPU、缓存、优化和 observer 条件一致。

三轮依次 A→B→C、C→B→A、B→A→C，各组先 CPU-off 后 CPU-on，共 18 份独立 flash/reset。工作量固定：1000 warmup、10000 RPC、5 s idle、100 Hz／200 calls、1000 paced Sample（至少 2 ms），32/120/512/1024 B 各三方向，1 s warmup＋5 s窗口＋完整 drain。所有组使用同一冻结的 gate harness，资格检查在放行 warmup 之前执行。主线程及全部 worker 的 policy/nice/affinity 每 50 ms 检查；系统设置在每次放行和结束核对，不调整优先级、频率策略、缓存、IRAM 或队列深度。

按每轮、每种 observer 分别计算 B/A 和 B/C，保留三次原始配对和配对比值的中位数，不混合样本。主要目标约 40%，35–40% 可评审；libcsp 成本≤1.2、goodput≥C/1.2 分别报告，不作为硬继续门槛。保护沿用 RPC-off p99／低频 p99、两种 observer 的 Sample API p50/p99、host 每 RPC／低频调用／Sample 的全线程 CPU、MCU-on Sample active、每个 stream 方向分量 goodput 的 5%，idle 为 0.5 个百分点。参考组相应指标三次 max/min 超过 1.05（idle 跨度超过 0.5 pp）则不可判定；稳定 A 上至少两次配对越界构成重复保护失败，未解释前停止 K5。单项异常、噪声和主目标分别呈现。

stream 工作量随吞吐变化，报告全阶段／窗口 host CPU、MCU 窗口 active、双向分量和全部字节账本，不将更多吞吐所消耗的绝对 CPU 百分比冒充等工作量退化。保留所有 RTT 样本、每千次分段、max、IRQ、资源和栈数据。损坏输入校验已通过：拒绝 9999 RPC、过短 Sample pacing、单 slot、错误 stream 账本、缺失大包矩阵、短窗口；极端 RTT 保留。此校验使用明确标记的合成资格数据，不计入性能。

正确性／账本／源码／环境失败立即停止采集并保留原始尝试，先调查；替换需另行登记，不自动补测或挑选样本。K4 通过后仍须 K5 默认迁移和最终源码验证，不能把原型成绩称为最终产品成绩。

## 环境块重新登记（正式工作量开始前）

首份 `k4-r1-A-o0` 在 warmup 放行前触发 `pre-warmup thread/system drift`，没有进入测量；原始 manifest／console／host 输出和最初登记全部保留。后续只读检查发现当前工具进程继承 nice 16，先前资格冻结为 -4，governor、EPP、affinity 无变化；旧失败工具没有在异常分支落盘实际签名，不能声称已直接测得失败 host 的确切 nice 或定位了外部变更来源。

按计划的环境变更规则，在 `k4-environment-rebase/` 单独建立完整新块，原六份 binary／路径、镜像、工作量、判定阈值及轮转顺序完全相同，不混入旧块。六组重新完成 smoke，全体 main／worker 都实际保持 policy 0、nice 16、affinity 0–5，系统设置与初始登记一致。没有通过 renice／改名／改路径／改 governor 寻找有利设置。新 collector 额外在检查前保存实际环境，以便今后失败可审计。

新块 `k4-registration.json` SHA-256：`9d8e0c483a20802bae1d3cd835e853088ddec75c8f652da6ec6502baedd68a97`。六组环境与 collector 新哈希已冻结，初次未放行尝试不算性能数据。这是针对实测环境变化的一次完整重新登记；如再次变化继续保留失败并调查，不无限变换条件追数。
