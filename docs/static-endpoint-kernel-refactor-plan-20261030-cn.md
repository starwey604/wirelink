# 静态端点内核重构与语义裁剪执行计划

日期：2026-10-30。状态：待执行。本文交给实施 agent，覆盖 K0–K6。用户已接受破坏性重构和下述语义裁剪；最新要求为本轮改善约40%即可，不设置此前50%以上或libcsp 120%的硬继续门槛。

## 1. 这一次做什么

**用一个由 WLC 静态连接的端点执行内核，替换现有 link/event → endpoint/pump → generated runtime → RPC pending/cache → response service → link TX 的通用执行结构。** 同时删除 replay、链路ARQ、服务端延期回复和通用业务调度等语义；不是再给旧引擎加几个编译期开关。

本轮追求一次成体系的收敛：减少正常请求经过的逻辑对象、状态转换、结果转换和代码范围。静态生成器负责已知路由、容量和部署连接；紧凑C内核负责有界调用关联、数据所有权及I/O推进。

目标平台为当前 ESP32-S3、USB FS、240 MHz、16 KiB/8-way/32 B I-cache、`-Os`、LTO off。不得增加cache、把core搬入IRAM、改变主机调度或USB配置来代替结构收益。保留portable C11、无运行期堆分配、OS／驱动在adapter外层的边界。

### 1.1 本轮目标与继续规则

以开始执行时新冻结、新测量的旧实现A为基线，主要看：

- CPU-off RPC RTT p50：目标下降约40%。
- CPU-on MCU RPC active/call：目标下降约40%。
- MCU Sample active/frame与32 B吞吐：同步改善并保持其他负载不发生明显回退，Sample active下降约40%作为期望，不强迫其与RPC降幅完全相等。
- 功能、帧／字节账本、异步I/O、超时、关闭及资源所有权正确。

35%–40%的稳定主要指标收益属于可接受评审区间，不因少数百分点自动撤回；两项主要指标明显不均衡时分别报告，不用其中一项掩盖另一项。原型只有5%–10%收益时，先解释结构目标为什么没实现，不把它包装成此次激进重构完成。不是每个阶段都必须取得一次独立的百分比收益。

libcsp延迟／CPU的120%以内是后续对齐目标，不是本轮原型继续的硬条件。最近共同条件完整复测的预算参考如下，实际验收不得直接使用旧数字作新A/B：

| 指标 | 历史Wirelink | 下降40%的参考值 | 历史libcsp×120% |
| --- | ---: | ---: | ---: |
| RPC p50，µs | 1149.39 | 689.63 | 477.43 |
| MCU RPC active，µs/call | 1255.48 | 753.29 | 396.31 |
| MCU Sample active，µs/frame | 420.61 | 252.37 | 175.32 |

40%是目标而非实施前保证。若原型未达成，允许交付明确的失败／局部收益结论；不得通过语义外的隐藏削减、调频或修改工作量凑目标。

## 2. 已授权的破坏性重构

**允许修改公开API／ABI、生成代码合同、profile版本、内部结构和初始化方式；允许删除旧实现与旧调用路径，不保留legacy/fast双引擎、不保留自动回退。** 仓内消费者随新合同迁移，不能用同名旧API包装一整套旧执行引擎。

阶段原型与冻结旧A可以独立构建、独立运行；最终产品只保留一套新内核。实验隔离目录不是长期产品模式，不新增永久 `FAST_KERNEL` 选择。

### 2.1 产品语义删减清单

| 当前能力 | 新基础内核的决定 | 迁移责任 |
| --- | --- | --- |
| RPC replay、pending duplicate、canonical fingerprint、结果历史、TTL／淘汰策略 | **删除** | 幂等、去重、重试和持久操作键由业务实现 |
| link reliable DATA、ACK匹配／发送、duplicate、retry／ACK timeout、可靠TX handle状态机 | **删除** | 基础内核尽力交付；可靠传输将来可作为独立模块，不在本轮重建 |
| 服务端deferred handler、保存reply token后晚些complete/reject | **删除** | handler在当前调用内完成短回复；长任务返回job ID，由应用查询 |
| 服务端pending业务期限、到期token通知、业务取消联动 | **删除** | 业务任务自行管理；不是删除客户端RPC timeout |
| endpoint policy、多service激活／轮转／业务deadline管理 | **从通信内核删除** | 应用事件循环或独立运行时管理业务任务 |
| peer变化自动撤销业务任务／产品lease等广泛联动 | **删除** | 通信连接事件通知产品，由产品决定业务清理 |
| managed/mapped双套RPC执行模式、replay/correlate模式组合 | **统一为一种托管请求关联模型** | 既有协议字段映射在外围适配；不保留双引擎 |
| 泛化runtime结果union、默认全链路on_result、终态inspect/release强制流程 | **精简** | 紧凑返回码、typed完成回调、按需诊断；不丢弃真实错误 |
| LATEST/FIFO/outbox/bulk策略 | **不进入新基础目标** | 有独立工具价值者保留为独立目标；依赖旧owner的集成迁移到应用或删除 |
| 旧profile中要求上述能力的声明 | **明确编译失败** | 更新profile；禁止静默将reliable/replay/deferred解释为新语义 |

不要求本轮为每项已删除能力补一个可选插件。尤其不能又写一个新的通用服务调度器、replay模块或可靠协议层，绕回原结构。

### 2.2 明确保留的基础合同

- 普通telemetry／消息收发、非阻塞客户端RPC、同步短时服务端handler。
- 客户端固定有界并发，验收配置仍为**4个调用槽**；两端均能发起和处理RPC，不能把设备偷偷改为server-only。
- 请求／响应message类型与调用编号匹配、本地timeout、取消等待、远端业务拒绝与本地错误的区分。
- 客户端实例身份及本地generation，旧响应不能完成新实例／新调用；旧callback／token不能操作新资源。
- 接收边界、长度、容量、codec和完整性校验；公共业务codec及schema合法值域不缩小。
- RX借用生命周期、TX源内存有效期、claim／abort、BUSY和排队、物理consumed/drained、旧completion拒绝。
- 通知与等待竞态、有限deadline、stop/close/reinit、无工作时休眠。取消是本地取消，不承诺回滚远端副作用。

服务端取消延期回复不等于传输改成同步：handler的响应可能在BUSY时等待提交、在DMA/USB发送中等待完成，这些存储和生命周期必须保留。

## 3. 端点与wire合同

### 3.1 一种RPC身份与编码

本轮优先沿用当前托管RPC wire元数据和业务私有codec，保留原client session＋operation ID、请求／响应ID和业务状态规则。保留现有USB native普通DATA字节格式，基础payload schema、public codec和消息ID不变。这样旧host可以与新device通信，便于隔离MCU收益。

不重新设计varint、TLV、字段presence、未知字段处理或整数值域，不把改成裸结构体传输当作本轮收益。独立framing／CRC／COBS工具及冻结合法业务向量仍可保留；它们不构成旧link ARQ运行时。新端点接收可靠DATA／ACK等不支持的类型时明确拒绝并能继续收取普通DATA。

新生成API／部署manifest与旧版本不兼容，应提升相应合同版本。旧mapped／guarantee／reliable配置显式诊断；若当前profile需要删去这些字段，归档迁移前后声明，不能修改业务内容或悄悄切成其他语义。

若保持现有托管wire确实妨碍内核结构，先给出具体阻碍和独立wire提案，停止该项扩大；不能在未登记时同时改wire与执行结构后混作等wire结果。

### 3.2 新客户端调用合同

提交成功前将请求编码／快照到端点有界存储，返回后用户请求对象可失效。BUSY未接收请求则不生成调用、不消耗slot、不在以后触发回调；已接收但等物理TX的调用，其timeout包含排队时间。

每个被接收调用恰好一次本地terminal完成：成功、业务拒绝、超时、取消或连接结束。完成回调运行在owner，结果借用至回调返回；用户需要保存时显式复制。删除“必须长期保留终态slot等待inspect/release”的要求，handle在终态后失效。同步等待包装只在外层，不能成为core另一套状态机。

回调可以提交／取消其他调用，但不递归step/close；提交不得内联执行接收或terminal回调。取消引发的其他回调也由受控owner推进，避免共用结果scratch被嵌套覆盖。新调用不得被同一轮旧slot遍历误判为过期，使用generation／本轮快照保证。

保留operation ID耗尽的明确错误，不回绕复用成旧请求；连接实例身份由初始化环境提供，不在收发热路径取随机数。不在RPC请求上自动切换／清理业务peer状态。

### 3.3 新服务端合同

handler使用类型化请求和可写响应，返回成功／业务状态；不向外发放reply token。请求借用只在handler期间有效，持久业务任务自行复制参数。

**调用handler之前必须有响应构造／发送存储容量。** 如果尚无容量，按端口RX背压保留该请求，或明确拒绝admission；不能执行副作用之后才发现没有位置放回复，也不能为了吞吐丢掉合法请求。

handler只执行一次。编码后的响应在BUSY时保留，由内核继续提交，不重新调用handler或重新执行业务。数据被sink接受且内存租约结束后即可回收，不为未来重复请求保留历史。重复请求只要独立admission就允许再次执行，这是新的明确语义。

encode／容量／handler失败都有确定的错误或业务拒绝路径，释放预留资源；不能返回伪成功。不得绕过真实handler来直接拼benchmark回复。

## 4. 静态端点内核结构

### 4.1 WLC负责静态连接，C内核负责最少状态

WLC输出：

- 类型化收发与handler入口；按message ID的直接路由。
- 固定request→response匹配、解码／编码函数、真实容量上界、错误映射。
- 本端实际选定的envelope/integrity连接、角色和资源布局；有限调用／响应槽及所需scratch。
- 小型端点step连接代码与配置manifest；已知连接不通过多级runtime dispatcher/hook转接。

共享C内核保留：

- 一份客户端调用表：身份、generation、deadline、回调及当前状态；不再另设core client表＋async job表来镜像同一次调用。
- 一组有界TX存储及描述符：请求快照、已构造响应或普通消息的拥有者；尽量直接构造最终frame，不再经过业务scratch→RPC缓存→link payload→link unit多次转换。
- 必要的RX单元借用、物理TX租约、连接状态和owner通知。

逻辑RPC终态与物理TX内存租约不能强行合并：调用已timeout/cancel时，USB/DMA可能仍读其源内存。用最少字段保留该租约，直到completion/abort确认；不能复用该内存或再次通知旧call。这里保留物理记录不是保留第二套RPC引擎。

### 4.2 一次正常请求的目标路径

```text
端口发布完整RX单元
→ owner取得借用
→ 校验frame／RPC元数据
→ 生成的message分派与一次业务解码
→ 同步typed handler
→ 在已预留TX区域编码响应
→ 当前owner链内尝试提交
→ 归还RX；物理发送完成后回收TX区域
```

普通Sample／StreamChunk直接通过生成codec和TX存储进入同一端口，不经过RPC调用表。RX业务回调返回后释放借用，需持久保存的业务由应用复制。

正常同步RPC不再经过generic event结果发布、runtime admission token、response acquire/defer/submitted/sent、单独的RPC service几个独立对象／API链。BUSY恢复通过同一TX拥有者状态完成，不调用旧 `wl_rpc_server_*` 或旧pump作为后备。

先排出端口completion，再在固定budget内处理RX、已准备TX和到期调用；事件budget仍16，回调工作量必须有界。定义有界公平顺序，避免持续telemetry让RPC回复／deadline饥饿。没有证据表明旧实现多转很多owner圈，不能把“减少循环次数”当作唯一改动目标。

### 4.3 等待与deadline

端口提供必要的RX／TX活动与latched notify；callback/ISR只发布I/O状态，不在驱动线程执行codec、handler或RPC回调。core不创建线程、不读取系统时钟，由owner传入单调快照。

内核只维护通信工作的runnable和最早客户端deadline，不负责任意应用service的deadline。有限4槽的截止时间扫描可以共享小循环，不为了O(1)查询增加复杂timer结构。最早期限只在相关状态改变或到期后更新，最终等待仍观察最新活动，不能复用会漏唤醒的旧快照。

覆盖notify-before-query、query中发布、query后wait前发布、completion后资源恢复、stop唤醒和u32回绕。若端点内部有待发响应但sink仍BUSY，等待资源通知，不不断返回runnable制造spin。

### 4.4 端口／adapter边界

新端口只表达完整packet的RX借用、TX提交结果、completion/token、活动通知和关闭／恢复。保留必要的I/O回调和typed业务回调，不为每个byte或每层逻辑增加函数指针。

USB product adapter对接这个端口，不再要求一个完整旧 `wl_ctx_t` 才能收发。保留native边界、ZLP、读容量严格大于合法frame、错误后停止RX／明确恢复、claim/abort、延迟completion等现有合同。USB driver和Astrial底层传输不因新内核重写。

COBS UART等stream适配器在独立framing模块将bytes变成完整packet，再交给同一内核；native/length16使用对应有界模块。envelope/integrity由部署选择连接，不在正常每层反复读取selector。不要求第一版原型就迁移所有adapter，但产品替换前必须完成仓内受影响消费者，不能留下默认链接旧core的adapter。

已知framing选择必须到达实际机器码：只在生成C中写一个常量、随后仍调用单独编译的全模式公共 `wl_frame_encode`，不算静态连接完成。允许复用小型header／bounded writer／parser原语，以每种实际部署能力一份的紧凑连接函数表达native/NONE或COBS/CRC16等；同能力多个消息共享，不为每个方法复制全套codec/frame循环。独立公共工具可以继续通用，新内核不得依赖它恢复通用分派链。

## 5. 资源、代码与可移植性约束

- ESP32-S3验收保持2×2048 RX units、单RX请求／单TX在途、4个客户端调用槽、16事件budget；不能砍成单调用、加深USB队列或聚合多帧来取得成绩。
- 服务端不再有业务pending/replay池，但应提供足够的有界待发响应容量；登记原／新响应admission能力，至少能容纳4个已完成待发回复。TX描述符不得引用已经归还的RX或handler栈对象。
- 静态内存按真实codec／frame上界分配；不把每个pool都按全局最大StreamChunk盲目展开。报告全部库、生成对象、adapter、RX数据区、thread stack、heap；不把内存转移到应用就声称减少。
- A/B接收数据容量和物理I/O预算保持相同。新内核总静态RAM目标不超过旧集成；发现必须增长时先列出来源，不隐藏在benchmark／驱动／host中。
- 不再受旧896 B opaque context或原字段offset限制；允许新紧凑结构，禁止ABI兼容填充迫使新实现保留旧结构。
- 私有业务codec和公共codec原算法优先复用；不把小型codec作为首要重写目标。生成路由不为每个消息克隆完整poll／RPC／I/O引擎。
- 默认没有逐帧日志、完整泛化结果对象或启用全套observer才能工作；诊断按需构造，业务错误不能静默丢弃。
- 不改cache、IRAM布局策略、Flash频率、USB driver调度、主机nice/governor等系统参数；不搜索padding、对齐、函数顺序或强制内联组合。
- 现有平台编译选项留在构建边界。新portable core无ESP/Xtensa/Zephyr头、寄存器、ITCM/IRAM属性；不以新的section布局作为主要优化。

结构预算：以完整请求频繁执行的Flash行投影从约18 KiB向10–12 KiB收敛为设计参考，同时看动态工作和实际miss。该投影不是精确工作集，也不是硬性缓存容量证明；不以总.text变小或投影跨某数字替代实测。

## 6. 实施阶段与文件责任

实施者不是代码库唯一修改者；保留用户和其他会话的修改，不使用Git reset／批量restore清理工作区。`wirelink/wlc`是独立仓库，必须单独记录状态、patch和工具身份。

| 阶段 | 工作与交付 | 扩大实施条件 |
| --- | --- | --- |
| K0 | 冻结旧A、新合同、资源／调用图和工作量登记 | 可靠／replay候选未混入A；保留独立正确性修复 |
| K1 | 新紧凑内核、必要WLC生成与USB端口，软件端到端原型 | 正常RPC/Sample和异常所有权可运行 |
| K2 | 当前板卡新A/B小矩阵，检查大幅收益与代价 | 约40%为目标，35–40%稳定收益可继续评审；不机械撤回 |
| K3 | 完成新内核剩余消息／并发／生命周期和host集成 | 全部保留合同通过，明确删除语义不被暗中恢复 |
| K4 | 完整USB FS A/B及libcsp对照 | 真实工作量／资源／平台条件合格 |
| K5 | 删除旧路径、迁移所有受影响仓内消费者／文档 | 新实现独立构建，不依赖旧引擎 |
| K6 | 独立重放、结果与最终状态交回 | 默认产品改动与研究结论明确，不发布／部署 |

建议文件责任：

- `wirelink/src/`、`include/wirelink/`：新kernel、port、调用／I/O生命周期、旧link/RPC/pump替换。
- `wirelink/wlc/src/{rpc_plan,profile_semantic,endpoint_codegen,runtime_codegen,...}`：新生成合同、路由和容量；复用codec plan，不复制一套规则。
- `wirelink/adapters/`、`runtime/`、`cmake/`、`zephyr/`：新端口映射及平台构建；不将OS规则移入core。
- `wirelink-cross-benchmark/usb/`、`scripts/`、`tests/`：真实产品集成、新能力身份／validator、旧A独立构建；工作负载与handler业务保持。
- `wirelink/tests/`、`examples/`、SDK生成与包消费：新合同功能验证和受管产物迁移。

本轮不改其他工程中的vendor副本、不做USB HS、不采购设备、不自动发布版本。新代码可以先放隔离原型目录，K5收敛到正式路径，不永久携带两套引擎。

### K0：冻结旧A与合同

新证据目录建议 `../wirelink-static-kernel-<实际日期>/`。从开始时实际dirty源码冻结Wirelink、WLC、benchmark、Astrial、Zephyr/模块、工具链和生成物。当前 `unit_rx` 与TX active claim容量修复是独立正确性工作，核对并保留，必要时共同进入旧A；不得计为新内核收益。

归档基线ELF/bin/map/config、完整命令、原始源码与生成器二进制。新candidate独立构建，不能用后来已被修改的仓库重建A。两个版本若符号不同，不在一个可执行文件中链接两套内核。

先登记新API与删除／保留矩阵、wire兼容方式、pool布局、每个对象拥有者、state转换及连接重置语义。只有普通消息与同步RPC，没有新的隐式replay或ARQ。

### K1：端到端原型

原型至少包含真实生成的Setpoint、Control、Sample：Control用于identity、START/STOP和观测导出，不能手工旁路；Setpoint调用真实业务handler；Sample走真实生成send和新内核TX。其业务字段、取值校验与回调变换与旧集成相同。

K1中完成完整7种业务消息的生成／容量规划，StreamChunk/Progress尚未完成实板集成时如实标未完成。不能用手写benchmark专用RPC engine证明“生成内核”成功；不能从编译期得知Setpoint答案后直接回包。

新device与冻结旧host保持托管wire兼容。最初A/B使用**同一份旧host binary**，device分别旧A／新B，便于判断MCU重构本身。B的身份通过明确登记参数核对，不修改或跳过build ID检查；核对真实schema、wire、角色、资源及kernel版本。

软件原型至少验证：正常收发／业务拒绝、4个并发客户端与乱序响应、双向RPC、BUSY后的单次handler执行、TX异步完成、timeout/cancel后晚到响应、错误frame后恢复、资源满／释放、close/reinit旧token失效。端口用真实产品接口fixture，不复制一个简化伪状态机作唯一证明。

### K2：有界实板筛查

每个observer各一份冻结旧host，A/B共享。固定A→B→A→B→A，独立reset，共10份capture；1000 warmup＋10000 RPC、idle5s、100Hz/200 calls、1000 paced Sample，保持原Sample ≥2 ms pacing。K1若已经实现32 B流，再同批登记三方向筛查，否则留K3/K4，不伪报已覆盖。

RPC使用10000而非1000个样本，减少p99仅由极少调用支撑的问题；这不是允许剔除慢调用。所有原始样本、分位数、分段趋势、host all-thread CPU、MCU active、pacing、错误／字节账本保留。CPU-off和CPU-on独立测量，不相减拼residual。

主机实际main／worker策略、nice、affinity、governor/EPP在warmup前登记，期间核对；不在本轮搜索最有利系统设置。环境变更的数据不混组；需要重新登记就保留旧尝试并明确新块，不能无限改可执行名／路径触发不同调度条件。

筛查解释：

- 目标两项主要指标配对中位降幅约40%，两份B方向应一致；35–40%可继续，某一项较低则说明差异而非合成一个“整体百分比”。
- 正确性／资源泄漏立即修复，保留失败；无明显收益且正常路径仍进入旧引擎，应先纠正架构，不直接扩大迁移。
- RPC p99、MCU Sample active等可重复回退超过5%，或流goodput下降超过5%，标保护问题。单份异常不立即自动删除原型；最多一次事前登记的完整补充块，保留全部结果，区分重复回退与环境／尾部不可判定。未经解释的重复回退不能按“平均收益大”合并为通过。
- 基线max/min已跨对应保护门槛的指标标不可判定，不能池化样本制造稳定性；少量百分点的目标差距不触发机械撤回。

如主要指标仍只有5%–10%收益，交付正常路径和存储实际变化，停在原型待审核，不在同一轮连续换内联／布局／队列策略去追数字。若约30%等有意义但未到期望，同样交回价值评估，不丢弃证据或自动宣称达标。

### K3：完整新合同与可移植性

补齐所有消息、双向telemetry／stream／RPC、4槽并发、资源满和连接恢复。将host接到同一产品内核；保留K2旧host共同对照，另标新host／新device的集成效果，不能把host替换的收益全算给device。

主要功能矩阵：

| 范围 | 必须验证 |
| --- | --- |
| RPC关联 | 同时4call、乱序／重复／未知／错误类型／旧session响应、业务拒绝、本地timeout含排队、ID耗尽 |
| 明确删掉的语义 | 相同请求可再次执行；无history/TTL/fingerprint；旧reliable/replay/mapped/deferred声明失败；无隐式降级 |
| 同步handler | 响应空间不足前不执行业务；BUSY恢复不重执行业务；变长／最大响应；encode失败释放；没有可延后保存的reply token |
| 客户端生命周期 | 提交后用户请求内存可失效；terminal恰好一次；回调提交／取消别的call；取消与completion、timeout与RX边界竞态 |
| 数据所有权 | RX借用至回调结束，leased数据不覆盖；TX仍被sink读取时不能复用；同步／异步completion、重复／旧token、drain、abort |
| 有界推进 | 预算耗尽后仍能唤醒；telemetry持续到来时RPC／deadline不饥饿；BUSY不空转；跨deadline／u32回绕 |
| 关闭／重建 | producer先停、I/O排空／取消确认、late completion不访问已释放存储、close后API拒绝、同地址reinit旧身份失效 |
| USB | 双向63/64/65、127/128/129、511/512/513编码边界，MPS整数倍ZLP、最大合法frame、槽满恢复、断线／满请求故障恢复 |
| codec／frame | 合法业务wire向量、全整数域、required/presence/unknown field原合同、截断／超长／坏CRC／非法metadata，错误后可恢复 |
| 通用性 | 第二份非benchmark schema、变长字段、嵌套或重复字段以及不同容量，避免只对7个测试消息有效 |

GCC/Clang严格C11／warning-clean、公共头C++消费、主机单元／集成、Zephyr unit_testing/native_sim、WLC fmt/test/clippy及安装后消费按新合同运行。补ASan/UBSan适用的纯core／生成消费者检查，重点覆盖复用、截断与借用，不为支持旧语义保留旧引擎。

UART DMA/IRQ、已有实验polling与其他受支持adapter迁移到packet端口或独立framing层，至少完成构建和实际adapter软件fixture；现有硬件未连接的场景明确未实测，不冒称USB收益代表UART收益。独立LATEST/FIFO/outbox等目标不能再依赖旧pump才可使用；不相干工具不为缩小源码数而删除。

### K4：完整性能与libcsp对照

完整三组：旧A（旧host/device）、新B（新host/device）、当前libcsp C，各CPU-off/on、三轮，共18份capture。保持既有project Bulk、codec和业务实现，不为C换成手工echo或更轻协议。若无法完成C共同条件构建／采集，先完成A/B12份，libcsp目标标未验收，不用旧成绩替代。

每轮轮转顺序，例如A/B/C、C/B/A、B/A/C；每组先off后on，独立reset。工作量沿用FS完整负载：1000 warmup＋10000 RPC、idle5s、100Hz/200 calls、1000 paced Sample、32/120/512/1024 B三方向、1s warmup＋5s窗口及完整drain。

报告每轮及配对：RPC p50/p99、MCU RPC/Sample active、host all-thread CPU、低频RTT、Sample API、全部goodput、idle、资源和栈。B/A约40%是本轮主要目标；B/C≤1.2分别报告，各项未达不改写为“追平”。吞吐的120%开销对应goodput至少C/1.2，不能把吞吐比与延迟比混用。

RPC p99/低频/CPU/吞吐保护沿用5%有解释规则，idle active不增加超过0.5个百分点；不要求每个观测都改善40%。host已明显较轻的指标不强迫再减40%。有噪声项保持不可判定，功能通过、主目标改善、保护问题和跨库目标分别给结论。

性能构建不带逐函数／逐块新探针。独立诊断可在主要结果后做阶段边界IBUS计数、少量覆盖，必须匹配未插桩ELF，报告映射率和扰动；诊断不作为K2继续的高门槛，不用探针快慢替代真实负载。

### K5：删除旧路径并迁移

在已建立新端到端证据后，列出逐文件删除／替换表。重点是旧 `src/protocol.c` 中ARQ与通用event推进、旧 `rpc.c/rpc_async.c` 逻辑、`endpoint.c/pump.c` 通用编排，以及WLC pending/cache/runtime-service模板；实际文件可以合并／替换，不能保留旧引擎作为新内核隐藏依赖。

移除公开可靠发送／ACK handle、replay/cache/TTL、延期reply token、policy/service编排及mapped运行时接口；旧DSL请求明确报错。公开frame独立工具、FIFO等纯数据工具如有独立价值可以保留，不能把“符号仍存在”误判为旧执行链仍在。

迁移仓内examples、Zephyr samples、SDK/受管生成物、host executor/wait包装、包导出／CMake／Kconfig、benchmark及测试。新默认生成和安装目标只指向新内核；需要业务调度的sample把逻辑放回应用event loop，不在框架里另造service体系。

旧协议向量／历史报告只读保留，旧特性专用测试注明删除原因，替换为新合同与不支持声明的真实测试；不是直接xfail全部失败来交付。删除或修订公开文档的旧承诺，提供中英文迁移清单，特别说明重复执行、业务幂等、长任务job ID和callback结果生命周期。

K5后重新构建最终源码并做关键功能／性能确认，不能将删除前原型的数字直接当作最终产品数字。至少复测A/B主要RPC/Sample和32 B保护；若最终核心机器码／队列／host工作量等发生实质变化，重新执行受影响完整矩阵。避免只改文档也无限重跑硬件。

### K6：交付与结束

交付 `docs/static-endpoint-kernel-refactor-result-<实际日期>-cn.md`，包括：

1. 新语义／API／wire／资源合同，用户代码如何处理重试、幂等和长任务；哪些曾有能力已经删除。
2. **破坏性重构与删除清单**，旧路径不再可达／不再默认链接的证据，新调用链及共享实现位置。
3. 实际源码、独立WLC、工具链／生成器／生成物、ELF/bin/map/config、编译／链接命令和源码漂移核对。
4. 测试命令、通过／失败／跳过、原始采集、配对、范围、独立重放、损坏输入负例和hash/seal；原型、迁移前、最终树区分清楚。
5. A/B主目标是否约40%、保护项、libcsp120%各自结论，全部CPU／吞吐／资源代价，未定位的问题。
6. 若未达预期，说明是仍执行旧层、保留合同本身成本、I/O／布局敏感还是证据不足；允许交付没有可证大幅优化方案的结论，不编造保证。

本轮不自动发布、push或修改外部项目vendor副本。结束前关闭host I/O，记录板卡native serial、bin hash、build ID与最终smoke；保留本轮最后一个功能合格的新镜像并说明是旧A还是新B，不擅自恢复历史COBS/UART固件。

**本轮完成标准是：形成一套更小的通信产品合同，并由真实新内核承载；约40%的稳定主要指标收益足以评审，不要求先跨过50%或libcsp120%的门槛。**
