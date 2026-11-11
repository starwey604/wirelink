# Owner 阶段计数：原产品回退仍在，24项探针不能外推

> 本轮研究已收敛。下文实验命令及分支状态为历史记录，临时入口已删除；当前状态见[收敛记录](static-endpoint-cpu-convergence-20261111-cn.md)。

## 本轮收敛了观测方法，尚未分清原路径的频率与单位成本

按[上一轮检查](static-endpoint-required-fixed-stream-inspection-20261101-cn.md)，本轮给原 A／required-fixed B 的独立源码副本加入相同的阶段计数，完成 off/on 三对交错采集。随后补做一对**未加计数、原镜像身份的 A/B-on 控制**。

决定后续方向的结果是：

- 计数链路正常：owner、step、两次 service、send 结果及业务／USB账本一致。
- 带24项计数的 D-A/D-B 没有复现原 H2D／双向 D2H 回退；同状态两对 D-B 的 H2D 反而约快5%。
- 在同样的锁屏、DPMS关闭、电池供电状态下，未加计数的原 A/B 控制又出现 H2D −11.82%、双向 D2H −10.23%，H2D owner 单位计时增加约30%。

**因此，这版诊断计数不是原产品执行路径的透明观测，不能用它认定或排除原 A/B 的推进／重试放大。** 锁屏、DPMS关闭、电池供电也没有使原回退必然消失。当前仍需区分重复执行与每次执行变贵；没有新的产品优化接受结论。

首选下一步是将计数缩到 owner 本地五个字段、去掉热路径 helper 调用和原子操作，并先检查它是否保留原 A/B 对比方向。停止扩张24项计数，不改通知／kernel 行为，不为得到肯定结论补测或合并不同状态。

## 同一份诊断源码分别选择 A/B WLC

证据目录为 `/home/ww/codings/wirelink-owner-census-20261102/`。输入复制自上一轮已冻结的 `inputs/`，不是当前 dirty 工作区。A 使用保留的基线 WLC；B 从 `8ac90a9` 源码构建。当前 WLC `main` 仍为 `df02154`，实验优化未合入。

四个诊断固件为 `D-{A,B}-{off,on}`，240 MHz、16 KiB／8路／32 B ICache、`-Os`、LTO off、单核、原USB资源与双RPC角色不变。对应模式 A/B 共用原 gated host，没有 host 探针、额外 wire opcode、逐调用计时或 SDK 修改。

构建前核对了当前 Zephyr HEAD 与四处已有平台改动，均与原 A 冻结内容相同。生成文件逐项对照：D-A 等于保留 A 的生成文件，D-B 等于上轮测过 B 的生成文件。每种 observer 内，`kernel.c`、`packet_port.c`、USB adapter、backend、main、CPU observer、enqueue observer、计数引擎及生成 kernel 的对象 text／literal 均在 A/B 间相同。实际 codec 差异仍为原候选。

## 24个计数的窗口和含义

复用既有 lock-free relaxed atomic uint32 引擎；START 在原 CPU snapshot 后取计数基线，STOP handler 入口停止计数。快照在本单核配置下持 IRQ lock。计数包括准备、整个 stream 及 drain，而原 CPU／goodput 窗口仍是5 s。

字段覆盖 owner pass、adapter service／active service、step／budget work／empty step、RX release、TX submit／busy／publish／drained／take、wake、wait分支／timeout、Chunk与Progress的attempt／success／WOULD_BLOCK及send error。

- `step_empty` 是原 budget count 为零。计入 budget 的背压 RX 尝试不属于 empty，不能将它改名为“没有任何有效业务工作”。
- `service_active` 是原 RX-rearm 条件成立，不保证该次调用已经推进一帧。
- wake 是调用 owner 通知函数的次数，不是成功解除阻塞或 semaphore未饱和的次数。
- 在 STOP 已结束原 CPU 窗口、完成原回复格式化后，三页计数只输出到 UART。没有热路径打印，也没有给 wire 回复加字段。

不能把5 s窗口CPU除以含准备／drain的整段计数，或把 inclusive IRQ／线程区间相加成 exclusive 框架成本。START／STOP发生在step内部，budget-work累计可跨快照边界；本轮直接验证的调用关系仍严格为 `step=owner_pass`、`service=2×owner_pass`。

## 状态切换保留且剔除，没有凑成三对

先完成原 A-on smoke、四个 D smoke、A/B boundary，再按每个 observer 的 AB／BA／AB 执行。共用原 host 的 native worker 调度签名持续校验；host-state sidecar记录图形、电源与采样间隔。

| 对 | off | on |
| --- | --- | --- |
| r1 | 跨锁屏状态，剔除 | 锁屏、DPMS开，合格 |
| r2 | sampling gap或suspend，剔除 | 锁屏、DPMS关，合格 |
| r3 | 锁屏、DPMS关，合格 | 锁屏、DPMS关，合格 |

合格对均为电池放电。on的三对虽各自合格，但r1的DPMS不同，只能分成一对和两对；off只剩一对。没有满足同状态三对、各系列span≤5%的稳定计数比较。`sampling gap or suspend` 不能进一步定为已经发生系统待机。没有替换剔除对，没有把 DPMS开／关或不同供电状态池化。

## 诊断结果：更多pass不一定对应更慢

以下仅列锁屏、DPMS关的 on r2／r3，单位为**每整段成功数据帧**的次数（双向分母是收发帧数合计）：

| 方向／字段 | D-A，r2／r3 | D-B，r2／r3 |
| --- | --- | --- |
| H2D owner pass | 1.57249／1.57661 | 1.74219／1.75608 |
| H2D empty step | 0.67352／0.66822 | 0.77601／0.80146 |
| H2D active service | 1.00011／1.00011 | 1.00005／1.00005 |
| H2D wake | 4.06262／4.06263 | 4.06277／4.06276 |
| H2D Progress attempt | 0.03117／0.03117 | 0.03126／0.03125 |
| H2D Progress WOULD_BLOCK | 0／0 | 0／0 |
| D2H owner pass | 2.95909／2.95296 | 2.95670／2.95657 |
| D2H Chunk WOULD_BLOCK | 1.95889／1.95278 | 1.95656／1.95636 |
| 双向 owner pass | 0.59537／0.59216 | 0.58897／0.58576 |
| 双向 Chunk WOULD_BLOCK | 0.20769／0.20797 | 0.19855／0.20142 |

36个stream的成功Progress入队数都等于USB字节账本反推的普通Progress加一个END，成功Chunk也与业务sent相同；未出现send error或物理TX busy。原始84个screen阶段窗口及所有字段在 `result-with-control.json` 中保留，另有smoke计数记录。

D-B 的H2D pass／帧在这两对增加约11%，但同阶段吞吐更高，而非重现原产品的降低：

| goodput，kB/s（十进制） | D-A，r2／r3 | D-B，r2／r3 |
| --- | --- | --- |
| H2D | 194.144／193.427 | 205.363／203.034 |
| 单向D2H | 183.072／187.654 | 183.328／180.038 |
| 双向H2D | 117.472／117.600 | 116.070／117.811 |
| 双向D2H | 66.880／66.317 | 67.469／66.349 |

这些数据描述的是 D。既不能把D的约11% pass增量当作原产品约30% owner计时增量的解释，也不能因为D的双向pass未增加就排除原产品重复推进。

## 一对原镜像控制：当前环境中原回退再次出现

D未复现原差距，同时图形／电源状态与上轮不同，因此另行预注册一对 A/B-on 产品控制，不追加到旧验收矩阵。原 A使用现存恢复镜像；旧 B payload此前已清理，本轮从同一冻结源码重建，BIN大小145124 B，SHA256与旧测量记录一致：

```text
13991dab8ea6f2956daa32702c08f552ea45f32d4e307bdaa75b40221e607abb
```

第一次重建BIN身份不符。检查发现925个共同函数的操作／控制流／literal引用及入口地址均相同，配置相同；HAL `soc_init.c` 在 DRAM 启动日志中嵌入了 `__DATE__/__TIME__`。旧UART记录为 `Nov  1 2026 22:31:51`。为复现该日志字面量，设置 `SOURCE_DATE_EPOCH=1793572311`，只重编本轮自己的该对象并重新链接后，BIN身份与旧B一致。没有改SDK源码或修补固件字节；重建记录见 `control-B-on/reproduction.json`。

控制的两份fresh smoke、A/B screen与最终原A-on smoke全部完成。整个AB对保持锁屏、DPMS关、电池放电，sidecar判定合格；host与调度条件保持一致。

| 原产品控制，kB/s | A | B | B/A |
| --- | ---: | ---: | ---: |
| H2D | 200.474 | 176.787 | 0.8818 |
| 单向D2H | 158.803 | 156.659 | 0.9865 |
| 双向H2D | 124.493 | 117.312 | 0.9423 |
| 双向D2H | 68.429 | 61.427 | 0.8977 |

H2D owner inclusive-IRQ 计时为68.74→89.48 μs／等效数据帧，双向为90.93→102.75 μs；仍是按窗口CPU与同期goodput归一化，不是逐帧trace。Sample API的p50仍为A 1791、B 948 cycles。

这个单对控制没有建立新的稳定幅度，却说明：**在当前锁屏／DPMS关闭／电池环境中，原产品的回退方向依然能够出现；不能仅用这些环境状态解释D为何未复现。** D改变的代码布局、额外调用／原子工作及其时序交互仍是观测扰动的重要来源，尚未分离各自贡献。

## 下一项只保留 owner 本地五个字段

下一判别优先缩小观测面，而不是扩大重构：

1. 只计 `owner_pass`、Chunk attempt／WOULD_BLOCK、Progress attempt／WOULD_BLOCK。它们由同一owner写入，使用本地uint32直接增量；去掉每事件helper调用、原子操作及core／port／adapter内部探针。
2. step／service频率由现有一／二次调用结构推导；Chunk成功数沿用业务账本，Progress成功数沿用本轮已交叉验证的USB字节模型。
3. 先查实际机器码，保证热路径没有新增CALL8或原子循环；随后只做一次有限的匹配筛查。只有保留原产品对比方向，才使用计数回答频率问题。若仍使差距翻转，停止这条软件计数方法，不继续加探针或调布局找结果。

本轮没有实施这个新版本。当前24项D的计数与时间不用于接受 required-fixed；原A基线与候选未合入的决定不变。

后续[五字段owner计数](static-endpoint-owner-local-result-20261102-cn.md)已通过方向保留门槛并完成同状态三对：H2D外层pass／帧稳定下降约6.9%，Progress尝试密度基本不变。后续优先研究接收解码等既定路径的执行成本，不继续增加通知／推进探针。该轮按用户新要求保留最后健康固件，没有回刷原A；见后续记录中的固件身份。

## 工具、验证与清理

新增 `benchmarks/notification_progress/owner_census/` 的准备器、计数页校验、独立重放及用法；字节模型抽到 `a_residency/stream_ledger.py` 供两个工具共用，保留旧 `inspect_stream.py` 的函数入口。五项计数／稳定性单元测试和六项既有模型测试通过；原始重放验证25份采集、36个screen stream的Progress账本及两次回交原A-on。四项定向篡改／未完成恢复拒绝检查通过。

证据目录保留源码副本／补丁、编译器源码、原始host／UART、worker／host-state时间线、两个独立registration、source audit、反汇编／map／配置／编译命令、`result-with-control.json`及清理记录。未加计数控制的数据单独保留，不与诊断三对或旧产品三对合并。

本轮四个D构建、一个控制B构建和Rust编译缓存均已永久删除，共358534779字节逻辑文件量（约359 MB），未归档compiled payload。删除后原始重放仍得到同一结果。原A-on已恢复并通过最终smoke；当前使用的原A镜像与匹配WLC仍保留。

```sh
python -B benchmarks/notification_progress/owner_census/analyze.py \
  --evidence /home/ww/codings/wirelink-owner-census-20261102 \
  --out /absolute/new-owner-replay.json
```
