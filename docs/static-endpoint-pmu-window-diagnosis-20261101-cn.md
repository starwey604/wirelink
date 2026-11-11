# Sample 的 window exception 与 stall：重新登记后的 PMU 对照

> 本轮研究已收敛。下文实验命令及分支状态为历史记录，临时入口已删除；当前状态见[收敛记录](static-endpoint-cpu-convergence-20261111-cn.md)。

## 结论：异常次数不是本轮诊断镜像的分档解释，原 A 根因仍待定位

本轮完成了带硬件校准的 Sample 稀疏 PMU 测量，并在主机重启后重新做六组资格检查、登记环境和采完12份screen。**96个抽样调用的内部及外层 window exception 均为6次**，但耗时从912到1990 cycles不等。因而“慢档只是多发生了若干次窗口异常”不能解释这些诊断调用。

新数据同时限制了初测解释：此前一份CPU-on资格记录中的611／1107 cycles、stall差496的双档，没有在新块复现。新块的CPU-on抽样全部为1105–1107 cycles，且开启采样会改变未抽中调用的分布。**本轮探针不能外推为原A的低扰动替代，也没有定位到某条指令或原A的cache根因。** 异常处理本身的可变延迟、指令侧等待及探针对驻留／执行相位的影响仍需区分。

产品保持已接受的A；不扩大core重构，不改通知策略，不据此判定编译期专门化无效。O4/O5及120%目标没有新验收结论。

## 中断块与新块分开处理

证据根目录为 `/home/ww/codings/wirelink-pmu-window-20261101/`：

- 根目录保存第一版只读内部计数的校准探索；`research-tools/`保留对应源码。
- `r2/`扩展了外层window计数。早期资格记录发现611／1107 cycles双档；随后矩阵在串口／USB断开时中止，自动刷回A也因串口设备消失而失败。这不是完整对照，不纳入最终矩阵。
- 用户要求重新探查时，主机已经重启、设备重新连接。`r3/restart-context.json`记录新boot ID `86921daa-c4a2-4ae2-a1d1-dda3b676972c`。先恢复A-on并通过smoke，再做其余五组资格；六组均属于本次开机。
- `r3/`复用未改动的r2诊断镜像与host，不复用旧运行环境或性能样本。独立登记三轮QP／PQ／QP，off/on各一对，共12份screen。六对均有效，全程解锁、eDP-1亮屏；native三线程均policy0、nice−4、affinity0–5，电源状态未跨变。既有启动规则通过等待父shell生效继承，没有renice或更改系统策略。

最终结果为 `r3/result.json`；原始host／UART／逐线程记录、状态旁路、登记及资格manifest均在同目录。旧中断记录保留原样，没有拼接或补采来替代它。

## 只修改独立研究应用，硬件计数先校准

平台保持ESP32-S3、240 MHz、16 KiB／8路／32 B ICache、`-Os`、LTO off。USB FS、MPS64、2×2048 RX、1 RX／1 TX in flight、both角色、4 calls／4 replies、budget16、普通snapshot1均不变。

同一observer下A/Q/P共用一个host，使用环境变量`WL_PMU_PROBE`：

- A为原产品镜像，值−1，不发送私有命令。
- Q为诊断镜像，值0：PMU自由运行、阶段计数和Sample尝试账本，无逐API快照。
- P与Q是同一镜像，值1：每64次成功调用抽一次；1000次中固定16个索引，奇偶各8个，全部原始耗时仍导出。

PM0为EXR selector5／WINDOW mask0x20；PM1为I_STALL selector4／ALL mask0x1ff。TRACELEVEL15、KRNLCNT0、overflow interrupt关闭。使用SDK的明确字段，不把组合别名当selector。ALL包括指令相关及其他global stalls，不能等同于cache等待；字段及API见[Espressif perfmon文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/perfmon.html)和[官方事件定义](https://github.com/espressif/esp-idf/blob/master/components/xtensa/include/xtensa/xt_perf_consts.h)。实际使用本地SDK头文件，原始配置与机器码证据保留。

每次启动在USB开始前短暂锁IRQ做校准：先拒绝已被占用的PMU，再比较PMU cycles与CCOUNT，检查32次浅／深CALL8链、除法stall、control读回以及32次空快照。业务测量不屏蔽IRQ。最终12份校准如下，Q/P各自一致：

| 校准量 | off | on |
| --- | ---: | ---: |
| CCOUNT／PMU cycles | 66539／66374 | 66429／66216 |
| 浅／深链window事件 | 0／1666 | 0／1666 |
| 除法stall | 2816 | 3310 |
| 空快照最大elapsed／stall | 737／418 | 995／562 |
| 空快照内部／外层window | 0／0 | 0／0 |

空快照包含其初始化与驻留条件，最大值甚至大于部分业务调用；**没有将它从业务耗时中相减**。

## 计数范围与原读钟数组逐项对齐

内部快照位于原两次CCOUNT调用之间，读window、stall、IBUS miss及WINDOWBASE／WINDOWSTART；外层再读PM0，覆盖两次读钟调用。实际反汇编中，外层括号只有原两次clock调用和一次`send_message`，六条RER，没有新增CALL8／CALLX8。探针仍改变指令、寄存器分配及链接结构；抽样elapsed包含快照工作。

16条记录逐项匹配原Sample数组的索引、start与elapsed。读取顺序不同的window／stall／miss区间不能相加或相减为独占成本；它们也都可能包含IRQ或抢占。IBUS miss没有PC信息，可能发生在业务、异常处理或探针指令中。

每份Sample均成功1000次，Q/P的attempts均1000、busy和errors均0，排除了本轮D中未记录的admission重试。A没有新增账本，不能把这个排除自动推广到A。六份on的Sample USB IRQ均1002次；次数相同仍不能推出中断位置和耗时相同。

## 96个抽样调用：次数相同，stall和miss并不相同

所有记录的`WINDOWBASE／WINDOWSTART`前后均为`10／1024`，内部／外层window事件均`6／6`。下表仅为P，单位均cycles，miss为IBUS事件数：

| observer／轮次 | 样本数 | elapsed | I_STALL_ALL | IBUS miss |
| --- | ---: | ---: | ---: | ---: |
| off／1 | 8 | 912 | 3 | 0 |
| off／1 | 8 | 1989 | 1082 | 3 |
| off／2 | 16 | 1989–1990 | 1082–1083 | 3 |
| off／3 | 16 | 1989–1990 | 1082–1083 | 3 |
| on／1 | 16 | 1105–1107 | 497–499 | 1 |
| on／2 | 16 | 1105–1107 | 497–499 | 1 |
| on／3 | 16 | 1105–1107 | 497–499 | 1 |

off第1轮两组elapsed差1077、stall差1079；这些区间不是同一端点，不能要求差值严格相等，更不能据此把三个miss换算为固定延迟。它们支持“指令侧等待值得继续查”的候选，却没有给出miss发生位置。

r2的初测CPU-on样本曾是611 cycles／stall3／miss0与1107／499／1，window同为6／6。新块CPU-on没有复现这对档位。应保留这个反例，而不是继续把611／1107命名为稳定hit／miss模型；同一诊断镜像可出现不同分布，前序状态及执行相位是待区分的因素。

## 稀疏采样仍会影响未抽中的984次调用

对Q也排除相同16个索引，保留首调用，得到以下未抽中984次的均值。它们不是独立未受扰动的对照：P阶段仍执行了另外16次采样。

| observer | 轮次 | Q均值cycles | P均值cycles |
| --- | ---: | ---: | ---: |
| off | 1 | 2045.600 | 1418.282 |
| off | 2 | 1506.682 | 1958.361 |
| off | 3 | 2045.874 | 1958.778 |
| on | 1 | 826.140 | 1072.598 |
| on | 2 | 1073.098 | 1072.976 |
| on | 3 | 814.530 | 1072.976 |

例如on第1轮，Q有500次564和499次1059 cycles，加一次首调用；P的未抽中普通调用却都在1058／1059附近。差别不限于那16条带快照的elapsed。off也在不同轮次改变主档。

全部1000次的Sample API均值，off的Q／P轮间跨度分别35.79%／38.09%，on的Q为31.80%；均不满足既有5%稳定门槛。on Sample active的P/Q中位比为0.98046，RPC active为0.99936，两项自身稳定，但不能覆盖API分布的反证，也不构成产品收益。完整直方图、CPU、RTT、idle与吞吐系列在JSON中，不另造2%产品验收结论。

此外，新资格中A-off为556 cycles×999，Q-off为2121×998；A-on主要1791，Q-on为564／1059。资格不是正式Q/A性能矩阵，但已足以拒绝原指令路径／成本等价的假设。Q/A同时改变应用结构、链接驻留和阶段外工作，不能只归因于布局。

## 下一步先筛选原A的指令驻留候选，不再堆逐调用探针

本轮把“额外窗口异常次数”从D的优先解释中移出；还不能排除相同次数下异常处理自身等待不同。原A上的关键未知仍是：慢档在哪里产生指令侧等待，以及observer构建／执行如何改变它。

下一判别应先使用现有A-off/on的实际ELF、map和调用链，筛选业务编码、窗口异常处理及相邻observer代码的指令驻留候选。动态证据必须重新说明其对原main栈帧、布局和执行相位的影响；本轮不能以追加更多API内读数替代这一约束。暂不针对某个探针镜像做padding、函数重排、IRAM或inline优化，也不把inclusive stall推算成可回收通知CPU。

原A静态筛选已完成，见[实际机器码与驻留候选](static-endpoint-a-residency-analysis-20261101-cn.md)：11个Sample业务函数的归一化指令结构相同，读钟与窗口向量已在IRAM，on的成功路径行足迹反而更小。优先候选转为编码桥接／叶函数／literal行与前序owner、USB、周期observer的驻留交互；低位index模型仅用于筛选，未确认为硬件冲突。

## 检查、恢复与删除编译payload

- 两版诊断应用的off/on及对应host构建通过。GCC／Clang严格C11、ASan/UBSan的状态、PMU占用、回绕、导出边界及raw拒绝测试通过。探针源码与用法在`benchmarks/notification_progress/pmu_sample/`。
- core、codec、packet port及adapter对象text／literal匹配绑定A build.json的既有O0记录；A对象早已删除，本轮复用该记录。原observer text也未改；on的`.literal.thread_snapshot`因源路径改变了字符串重定位addend。`r2/observer-string-referents.json`核对实际A/D链接引用，只有断言源文件路径不同，`observer-relocations.txt`及反汇编保留。
- 诊断镜像相对A的`.text`增加1888 B、BSS增加808 B，data和IRAM text不变（off/on相同增量）。配置、map、编译命令及section记录保存于各版`build-evidence/`，没有用源文件未改声称整个链接布局不变。
- 最终12份screen从raw独立重算通过，六份新资格及六对状态门禁通过；共享cache-phase重放入口的默认行为也用上轮12份raw回归通过。
- `r3/captures/return-A-on/` smoke通过，板上为原`kernel-usb-6b7697df31604f96-wirelink-on`，bin SHA256为`dc129aa2a42f32cb7a9d18e95e426e62f5664ccf1a7905bc7d8e4b1e2ad80275`。采集进程及I/O句柄已关闭。

实验编译树的删除及删除后重放收据见证据根目录`cleanup.json`；只保留源码、原始记录及必要文本证据，不留固件／host编译产物归档。当前需要的原A和matching WLC继续保留。重放无需恢复实验镜像：

```sh
python -B benchmarks/notification_progress/pmu_sample/analyze.py \
  --evidence /home/ww/codings/wirelink-pmu-window-20261101/r3 \
  --out /absolute/new-replay.json
```
