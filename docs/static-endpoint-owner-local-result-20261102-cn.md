# 五字段 owner 计数：保留回退方向，外层推进没有放大

> 本轮研究已收敛。下文实验命令及分支状态为历史记录，临时入口已删除；当前状态见[收敛记录](static-endpoint-cpu-convergence-20261111-cn.md)。

## H2D先转向执行成本，不继续减少通知或owner pass

按[上一阶段决定](static-endpoint-owner-census-result-20261102-cn.md)，本轮将24项全路径原子计数缩为owner本地五个uint32字段，先通过机器码检查和代表性门槛，再完成CPU-on三对AB／BA／AB。

**最小计数版保留了H2D和双向D2H的回退方向；同状态三对中，H2D的owner pass／成功数据帧反而下降约6.9%，Progress尝试密度基本相同，WOULD_BLOCK三轮合计A/B各2次。** 这降低了“外层推进或发送重试放大”作为主要解释的优先级。

下一步优先研究既定执行路径的单位成本、驻留／总线／调度交互。计数覆盖的是整段外层pass与发送尝试，不是内层扫描、IRQ或逐函数执行trace；本轮没有确定具体cache事件，也没有把所有内部执行频率判为相同。

本轮按用户新要求**没有恢复原镜像**。板上最后健康跑完的固件为 `owner-local-20261102-B-on`，是五字段诊断版，不是已接受的产品基线。WLC `main`仍为`df02154`，原required-fixed候选仍未合入。

## 五处直接增量，没有新增热路径调用或原子循环

冻结输入仍为 `/home/ww/codings/wirelink-required-fixed-20261101/inputs/`。只改独立副本中的 benchmark backend／main，增加私有 `owner_local.h`：

| 原始字段 | 写入位置 |
| --- | --- |
| owner_pass | ub_backend_service入口 |
| chunk_attempt／chunk_block | Chunk发送分支入口／返回WOULD_BLOCK时 |
| progress_attempt／progress_block | Progress发送分支入口／返回WOULD_BLOCK时 |

这些字段只由同一个benchmark owner写入，START／STOP快照也在该owner上执行。使用普通无符号增量与模2³²差值，没有全局原子RMW、IRQ读者、每事件helper、内核／packet／adapter内部探针或host修改。成功入队数由attempt−block推导，并沿用原健康检查拒绝其他发送错误。

实际Xtensa审计结果在A/B中相同：

| 热函数 | 原函数体 | 五字段函数体 | entry栈帧 | 调用列表 |
| --- | ---: | ---: | --- | --- |
| ub_backend_service | 271 B | 283 B | 144 B，不变 | 不变 |
| ub_backend_send | 148 B | 190 B | 64 B，不变 | 三个生成send目标不变 |

计数是直接load／add／store，没有新增热路径helper调用、S32C1I／MEMW／RSIL。START／STOP的快照辅助函数不在这两条热路径中。比较时保留未解析间接调用的寄存器表示，不凭附近符号猜callee。

core、packet、USB adapter、RPC角色与预算、CPU／enqueue observer、正式生成kernel均未修改。八个非codec对象的text／literal在local A/B中相同；生成文件分别等于原A／上轮B的保留内容。A使用匹配的保留WLC，B使用`8ac90a9`生成器源码。

## 门槛先登记，再决定是否继续两对

本轮只回答CPU-on的两个主要回退，不启动off矩阵或完整O5接受试验。四份fresh smoke（两个原产品、两个local）及两个local boundary完成后，预注册：

1. 使用原BIN身份的产品A/B做一对控制。B从原冻结源码、原编译日志字面量设置重建，BIN大小／SHA256与上轮产品B记录相同。
2. 只有控制整对状态合格，且H2D／双向D2H两个B/A都小于0.95，才跑local AB pilot。
3. pilot必须状态合格、与控制图形／供电状态相同，两个目标比值也都小于0.95；否则停止软件计数方法，不换配置或补对。
4. 门槛通过才运行剩余BA／AB。计数推断仍要求同状态三对、A/B各自归一化系列span≤5%。

控制和三对local均为解锁、DPMS开、接电且电池Full；boot ID一致，各整对无采样断档或状态切换。共用同一个原gated host，native worker调度签名持续一致。

| 比较 | H2D B/A | 双向D2H B/A |
| --- | ---: | ---: |
| 原产品控制，单对 | 0.8802 | 0.8922 |
| local r1 AB | 0.8096 | 0.7870 |
| local r2 BA | 0.8589 | 0.8018 |
| local r3 AB | 0.8888 | 0.7974 |

门槛只证明两个目标对比的方向保留，不证明诊断版与原产品的所有时间、频率或机制等价。local A的单向D2H约211 kB/s，而原控制A约157 kB/s；local中新增了明显的单向D2H对比差距。这些时序变化仍要保留，不能把local计数写成原固件的逐帧trace或产品性能成绩。

## H2D：更少的外层pass，同样的Progress尝试密度

以下分母为START／STOP整段成功数据帧数，包括1 s准备、5 s窗口和drain。H2D为设备接收帧数；双向分母为收发合计。

| H2D指标 | A，r1／r2／r3 | B，r1／r2／r3 |
| --- | --- | --- |
| 成功数据帧 | 44331／44304／43158 | 35945／37913／38142 |
| owner pass原值 | 84460／84544／81941 | 64550／67087／67433 |
| owner pass／帧 | 1.90521／1.90827／1.89863 | 1.79580／1.76950／1.76795 |
| Progress attempt原值 | 1387／1387／1351 | 1125／1186／1192 |
| Progress attempt／帧 | 0.031287／0.031306／0.031304 | 0.031298／0.031282／0.031252 |
| Progress WOULD_BLOCK原值 | 0／1／1 | 1／1／0 |

owner pass的三轮配对比中位数为0.93117，A/B系列span分别0.51%／1.58%，满足本轮稳定性条件；Progress attempt密度的配对比中位数为0.99922，两侧span也小于5%。H2D没有设备Chunk发送，Chunk两个字段均为零。已测外层pass、推导的一次step／两次service，以及Progress重试均没有放大到能直接解释回退的方向。

r3-B整段Progress block为零，H2D回退仍存在；该例不要求Progress入队失败才能出现回退。计数不能判断每次wait的持续时间，调度相位仍属于后续执行成本线索。

同一local构建的5 s窗口owner inclusive-IRQ计时，按同期goodput折算每等效帧为A 57.16／57.35／57.49 μs、B 66.18／66.74／66.34 μs。它与整段计数不是同一个窗口，**不计算二者相除的“每pass成本”**。它表明回退与较低的整段pass密度同时出现，使单位执行成本线索比继续合并通知更值得优先检查。

## 双向保留方向，计数稳定性仍有限

| 双向指标，次数／合计数据帧 | A，r1／r2／r3 | B，r1／r2／r3 |
| --- | --- | --- |
| owner pass | 0.76789／0.75647／0.75188 | 0.60644／0.57619／0.57305 |
| Chunk attempt | 0.74285／0.72984／0.72604 | 0.57949／0.55087／0.54795 |
| Chunk WOULD_BLOCK | 0.35214／0.34472／0.34224 | 0.22340／0.19890／0.19603 |
| Progress attempt | 0.024770／0.025429／0.025045 | 0.024912／0.024247／0.024687 |

三轮B的owner pass与Chunk重试均更少，但owner pass的B系列span为5.83%、Chunk attempt为5.76%、Chunk block为13.96%，没有通过稳定性条件，不把描述性比值当作稳定幅度。双向收发组成也不同；因此本轮用H2D作主要干净比较，不把两种方向的处理成本混成一个固定模型。

单向D2H的Progress attempt每份都是一个END边界事件。B成功数据帧较少，导致其“每帧Progress密度”配对比约1.28；这不是更多发送尝试。重放保留原值、比值及stable结果，另标为`phase_end_marker`，不作为热路径频率放大证据。

## 下一步先减少可确定的每帧工作，不改通知语义

优先核对H2D实际每帧经过的接收链：`usb_kernel_route(case103) → stream_chunk_decode → wlc_decode`，其中的字段查找／值读取／required检查，以及后续`on_chunk → receive`。这些入口已在冻结生成源码和backend中核对。设备的`measure／emit`属于D2H发送链，不能拿它当H2D每帧成本。

下一候选应从**原A源码**单独派生，先离线核对接收解码的重复工作和实际指令／调用数，再决定是否做小范围生成器专门化。保持原业务校验、数据复制和字节账本，不通过删benchmark工作取得收益；不叠加尚未接受的required-fixed B，不重做已经拒绝的通知／空调用快路径。

后续[H2D接收链离线检查](static-endpoint-receive-inspection-20261102-cn.md)已完成：规范32 B Chunk不进入字段查找；三个单字节varint仍各执行52条镜像内指令并调用ROM移位helper。下一候选收缩为原A上的通用reader单字节返回，不扩大为完整schema解码器重构。

减少确定性工作与定位原波动根因是两个评价层次。后续仍以未加计数产品构建的CPU、RTT、全部stream分量、资源和既有保护线决定是否采用，不能凭本轮计数接受代码。

## 当前固件、证据与清理

- 板上最后健康完成：`owner-local-20261102-B-on`，诊断版，最后采集`r3-B-on`。没有收尾回刷、额外恢复smoke或更改产品接受状态；控制阶段刷原A/B是实验本身，不是结束后恢复动作。
- 证据目录：`/home/ww/codings/wirelink-owner-local-20261102/`，核心为`final-registration.json`、`source-audit.json`、`result-labeled.json`、`status.json`及原始采集。
- 新工具：`owner_census/prepare_local.py`、`local_rows.py`、`local_gate.py`、`analyze_local.py`。旧24项工具的原始重放含控制数据保持一致；共享读取器按明确格式选择计数schema，没有改变旧指标含义。

九项准备／计数／门槛测试通过，其中snapshot的无符号wrap／差值由GCC和Clang以严格C11实际编译运行。四份smoke、两个boundary及八份screen健康检查通过；18个local stream的成功Chunk与Progress推导分别和业务／USB账本一致。诊断编译日志字符串按固定的`SOURCE_DATE_EPOCH=1793664000`显示为`Nov  3 2026 00:00:00`，不是实际时刻；本轮实际构建／采集在11月2日，UTC时标保存在manifest／status中。

三个新构建目录和Rust缓存已永久删除，共265206670字节逻辑文件量（约265 MB），不归档固件／ELF／对象；删除后原始重放结果一致。片上保留已运行的B诊断程序；旧A资产继续作为明确需要时的恢复／科学参考，不自动刷写。文本配置、map／反汇编、源码、原始数据及固件身份记录保留供复算。

```sh
python -B benchmarks/notification_progress/owner_census/analyze_local.py \
  --evidence /home/ww/codings/wirelink-owner-local-20261102 \
  --out /absolute/new-local-replay.json
```
