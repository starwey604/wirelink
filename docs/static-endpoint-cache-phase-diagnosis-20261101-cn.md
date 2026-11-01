# Sample 继续定位：启动优先级竞态已复现，阶段 miss 不能当作 API 成本

日期：2026-11-01。**保留 A，不进入产品优化。** 本轮排除了成功 Sample 编码的消息长度／奇偶分流解释，复现并处理了采集启动的 nice 继承竞态；同时测出了两种诊断构建的结构扰动。阶段计数能观察诊断固件的缓存行为，但还不能给原 A 分配独占函数成本。

证据目录：

- `/home/ww/codings/wirelink-sparse-sample-20261101/`：被拒绝的逐调用稀疏探针、资格记录、中断记录和源码。
- `/home/ww/codings/wirelink-cache-phase-20261101/`：阶段探针源码、资格记录、被环境门禁停止的块、优先级复现及清理记录。
- 后者的 `paired-stable/analysis.json`：最终 **12份同镜像 Q/P screen** 的完整重放结果。它是诊断对照，不是产品 A/B/C。

## 成功 Sample 的主体路径没有按奇偶帧分流

核对当前 A 的 schema、生成代码、core 和实际 ELF：

- Sample 的 sequence、sample_tick、value、flags 都是 fixed32；四个字段均存在，编码体20 B，加 native header 为24 B。数据值或序号奇偶不会改变这段编码长度。
- 成功调用使用同一个普通消息快照和 TX storage；没有奇偶帧双缓冲分支。admission 队列的9项环形索引是另一件事，不能把它叫作两个 Sample 槽。
- `wl_kernel_send` 在返回前编码、封装并入队，不在这段 API 中做 USB enqueue、net_buf 分配或等待完成。通知、前一轮推进与中断仍可影响它的执行环境。
- 原 A 的 `sys_clock_cycle_get_32` 在 ELF 中直接读取 CCOUNT；本轮没有发现墙钟校时参与该 API 差值计算。

原有数组只记录成功调用，不能由此排除成功前曾有未记录的 WOULD_BLOCK 尝试，也不能排除调用窗口异常、中断或工作集影响。

## 逐调用探针仍太重，收窄到阶段边界

第一版在独立源副本中按1/64分层、每层旋转17的位置读取 IBUS 计数，避免固定步长只采到一种奇偶。计数器读取放在原 API 两个时钟读取之外；软件状态只有 owner 写入。GCC／Clang严格C11及ASan/UBSan检查通过。

然而资格记录已显示：采样关闭的 Q-off，主要 Sample 耗时为1216／1217 cycles，而同 host 的 A-off 为556 cycles。P-on 还改变了未采样调用的分布。登记块开始后即停止，保留完整 A-off 和中断 A-on，不完成无解释力的三轮矩阵。安全退出关闭采集器并刷回 A-on，smoke通过。

第二版删除所有 Sample／owner-loop 探针，仅在应用 START／STOP 读取硬件计数：

| 组 | 固件／运行方式 |
| --- | --- |
| A | 原产品镜像；host不发私有诊断命令 |
| Q | 新诊断镜像，阶段读取关闭 |
| P | **与Q完全相同的镜像**，阶段读取开启 |

每个 observer 模式内三组使用同一个新 gated host。Q/P 均在启动时将计数器地址过滤器设为 `0x42000000..0x43ffffff`；不改 cache 几何、flush、IRAM、驱动或线程设置，不启用溢出中断。P在各阶段边界顺序读取 IBUS access/miss，使用 uint32 模差；没有新增逐调用采样或时钟。

即便如此，资格及首块有效 off 记录中，**Q-off 的主要 API 耗时1797 cycles，A-off为556 cycles，约3.23倍**。Q还改变了应用／链接结构、启动工作和阶段外控制流量，这不是纯布局隔离实验。因此阶段探针也不能称为原 A 的低扰动替代品；没有继续搜索 padding、函数顺序或 inline 组合来追求探针通过。

## 查明一处与锁屏无关的启动竞态

第一个阶段计数 A/Q/P 块实际跨过锁屏，门禁停止该块并正常交回 A。重新启动的 Q/P 块则在 warmup 前被拒绝：原登记 runner 全部 native threads 为 policy0、nice−4、affinity0–5，新进程实际为 nice16。返还 smoke 同样被拒绝，**flash已经成功，失败发生在主机环境门禁，不是固件故障**。随后在当前实际环境独立完成 A-on 健康检查。

本机运行 Ananicy，规则为 `bash -> Doc-View -> nice−4`；工具宿主当时为nice16。在一个不涉及锁屏的 shell 中复现：

| 时刻 | 父shell PID | 父nice | 新Python子进程nice |
| --- | ---: | ---: | ---: |
| 立即启动子进程 | 502958 | 16 | 16 |
| 等父shell已有规则生效后，约395 ms | 502958 | −4 | −4 |

原始值见 `launch-inheritance-repro.json`，规则快照也保留。启动时机足以改变继承条件，不需要锁屏发生；不能将不同块的 host RTT 直接混合。

新增 `wait_parent.sh` 只等待父shell符合已登记的既有nice，再fork采集器并保持shell存活；**不调用renice，不改scheduler、affinity、服务或系统规则**。实际 runner 的逐线程 pre-warmup／running检查继续保留。这次复用原六份正确性资格和同一批镜像，未把旧资格成绩重新标作性能样本。

## 最终六对数据均在锁屏＋灭屏、nice−4下完成

新块另行登记三轮 QP／PQ／QP，off/on各自配对，共12份完整screen。每对开始冻结会话状态；若跨状态，保留整对并排除比较，不补采，也不混合不同状态的中位数。实际六对均有效：全程锁屏＋DPMS灭屏，policy0、nice−4、affinity0–5不变。每份仍为1000 warmup、10000 RPC、idle、低频RPC、1000 Sample和32 B三方向stream。

P/Q增量结果如下；这些数值只描述这份诊断镜像：

| 指标 | Q三轮 | P三轮 | 结果 |
| --- | --- | --- | --- |
| off Sample API p50，µs | 7.4875／7.4875／7.4875 | 7.4875／7.4875／7.4875 | 完整1000次耗时直方图也逐轮相同 |
| on Sample active，µs/frame | 177.752／174.743／177.637 | 178.258／177.687／177.318 | 配对中位比1.00285，增加0.285%；本指标稳定且在2%目标内 |
| on RPC active，µs/call | 392.011／383.058／388.459 | 370.758／391.328／390.891 | P跨度约5.55%，不可判定 |
| on Sample API均值，µs | 12.1115／11.8032／13.2976 | 12.1073／12.1073／12.1115 | Q跨度约12.66%，不可判定 |

**没有获得整套低扰动通过结论**：Q/A结构扰动很大，部分P/Q指标也不稳定。完整host CPU、吞吐、idle、资源和保护判断保留在JSON中。六份on记录的RPC IRQ均为20002、Sample均为1002。

## 硬件 miss 总量与 Sample API 耗时没有一一对应

下表都是 P 的完整 Sample 阶段，含所有上下文及START／STOP边界工作，业务量均为1000帧。它们不是API独占计数，也不是retired instruction数量。

| 记录 | IBUS access | IBUS miss | API分布 |
| --- | ---: | ---: | --- |
| off第1轮 | 7,737,889 | 20,245 | 15605×1、2095×1、1797×998 cycles |
| off第2轮 | 7,742,734 | 15,753 | 与第1轮完全相同 |
| off第3轮 | 7,745,463 | 15,754 | 与第1轮完全相同 |
| on第1轮 | 8,000,430 | 32,837 | 主要2412／2413与3382 cycles交替 |
| on第2轮 | 8,003,155 | 32,846 | 同两档交替 |
| on第3轮 | 7,998,942 | 32,840 | 同两档交替，另一次2916 cycles |

off阶段miss从20245降到约15754，API的每个耗时值却完全相同。这直接限制了归因：**不能把阶段miss变化分配给API，更不能据此计算可收回的µs**。on/off构建同时改变observer执行与代码布局，也不能相减得出observer独占成本。

on的P前两轮均有500次2412／2413快档、499次3382慢档和一次首调用；第三轮快档变为499次，并多一次2916。因此p50从10.0542跳到12.15 µs，但均值只从约12.1073变为12.1115 µs。最近秩p50对边界的一次样本仍很敏感，完整序列比单个分位数更适合后续定位。

## 下一判别优先看调用窗口异常，而不是继续扩大探针

原 A 的实际 ELF 保留了多层 `call8/callx8`：main → send_message → backend →生成send → kernel send → encode_frame →编码回调 → sample_encode → scalar encode → measure／put32。芯片配置为64个物理地址寄存器、windowed ABI；SDK还提供 `XTPERF_CNT_EXR / XTPERF_MASK_EXR_WINDOW` 事件选择器。调用窗口溢出／回填因此是一个有具体机器码依据、尚未测量的候选，不应把两个稳定耗时档直接命名为cache hit/miss。

下一步优先用平台PMU区分window exception与指令侧stall，保持core无时钟／平台依赖；同时保留成功前admission重试这个未计数条件。若仍只有阶段总量，应继续报告范围限制，不做“消掉某几个通知就能回收多少CPU”的推算。O4/O5和120%目标仍未作新判定。

该判别已执行，见[PMU window／stall对照](static-endpoint-pmu-window-diagnosis-20261101-cn.md)：新块96个抽样调用的内部／外层window事件均为6／6，耗时却不同；新探针仍改变未抽中调用的分布。它降低了D中异常次数解释的优先级，尚不能把stall／miss归因推广到原A。

## 检查、交回与清理

- 两套研究固件／host的off/on构建通过。正式产品源码未改。core、packet port、wire、生成代码和adapter的对象text/literal与绑定原A build.json的既有O0记录一致；原A对象已删除，本轮复用该记录。observer text也相同，on的一个literal section因断言源文件路径不同而变；`observer-string-referents.json`核对了实际字符串引用，没有把该例外说成整个对象字节相同。
- 阶段诊断镜像的`.text`比A增加448 B、BSS净增24 B，data／IRAM大小不变；没有改变endpoint容量、角色、槽数、I/O预算或编译优化选项。
- 阶段探针的GCC／Clang严格C11、ASan/UBSan，以及状态／回绕／导出边界和raw拒绝检查通过。最终12份screen从raw重算，六份复用资格的身份、状态和raw完整性也通过校验；不是将中断块拼成完整矩阵。
- 最后一份 `paired-stable/captures/return-A-on/` smoke通过，板上为原 `kernel-usb-6b7697df31604f96-wirelink-on`，bin SHA256仍为 `dc129aa2a42f32cb7a9d18e95e426e62f5664ccf1a7905bc7d8e4b1e2ad80275`，采集和I/O句柄已关闭。
- 两套实验的固件／host构建树已永久删除，共4250个文件／链接，没有镜像或编译产物归档。还清掉旧`build/`树的64191个遗留缓存目录、2768个缓存文件及75134个空目录；它们大部分已无编译payload。源码、冻结fixture、原始测量及必要配置／机器码记录保留。当前仍需要的A和matching WLC保留。

删除实验构建后，12份raw再次重放通过。可用仓库中的入口，不需要恢复缓存：

```sh
python -B benchmarks/notification_progress/cache_phase/analyze.py \
  --evidence /home/ww/codings/wirelink-cache-phase-20261101/paired-stable \
  --out /absolute/new-replay.json
```
