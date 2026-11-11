# 单字节 varint 产品筛查：H2D CPU改善，保护项回退，候选不合入

> 本轮研究已收敛。下文实验命令及分支状态为历史记录，临时入口已删除；当前状态见[收敛记录](static-endpoint-cpu-convergence-20261111-cn.md)。

## 决定：保留合同测试，关闭这个组合候选的推广

已将[离线原型](static-endpoint-varint-feasibility-20261102-cn.md)移入正式WLC模板，完成生成器检查、产品链接审计、正式USB sample构建，以及无新增探针的CPU-off／on各三对AB／BA／AB筛查。

**主目标兑现了局部收益：CPU-on H2D每等效帧MCU active时间的配对比中位数为0.94265，下降5.74%。但CPU-off H2D、双向D2H吞吐分别稳定下降6.47%／6.63%，CPU-on Sample API p99为原A的2.448倍。按原保护线，不合入这个候选。** 不追加测量或修改稳定性条件追结果。

- WLC `main`为`559e2cc`：原生成器加reader合同测试，没有单字节优化。
- 候选V保留在WLC本地分支 `experiment/single-byte-varint-20261111`，实现提交`88fe65f296e9087195b15d241754a3d978829fd6`；测试先行提交为`930472c`。未合入main或替换保留的原A编译器。
- 板上保留最后健康完成的 **`varint-20261111-on`，是未接受的产品候选**，已不是上一轮owner-local诊断版。本轮没有收尾回刷或恢复smoke。

本次完成的是单候选32 B筛查，完整O5 A/B/C尚未执行；上述保护失败已足以停止该组合的直接推广。

## 正式模板改动同时进入普通消息和私有RPC

`wlc/src/codegen/templates/common.c.in`只增加8行：可读首字节小于128时推进at、写出64位值并成功返回；否则不改at／out，继续执行原多字节算法。没有改变wire、错误优先级、输出状态、公共入口、core／adapter／observer或编译策略。

正式生成与此前只改临时`usb.c`的可行性检查有一个重要范围区别：**共享模板同时生成普通codec和私有RPC codec。** 本轮实际变化是`usb.c`、`usb_kernel_codecs.h`及其来源manifest；移除这一个相同的新增代码块后，两份源码分别与原A完全一致。其他生成文件，包括公共头文件、values、kernel和deployment均相同。没有叠加旧required-fixed编码器候选。

产品输入使用`wirelink-required-fixed-20261101/inputs/`中冻结的原A core／应用／observer；两种observer各自共用保留的gated A host。当前Zephyr／HAL HEAD匹配原记录，四个原有SDK dirty文件逐字节匹配保留源码；没有改驱动、优先级、缓存配置或布局选项。USB仍为同序列号设备、FS 12 Mbps、MPS64；本轮物理路径为`3-1.4`，没有把历史`3-2.4`写成当前拓扑。

## 合同、生成输出和实际机器码

新增正式测试先在原生成器上通过，再运行候选。Schema-v1、ManagedTagged、positional-v1三种生成模式均由GCC／Clang以严格C11、`-Os -Werror`和ASan／UBSan验证，每组合386092次reader差分；Schema-v1另有22383次Chunk差分。比较status、最终at、失败输出，以及Chunk的presence／部分输出状态；生成器重复生成的结果也直接比较。

完整Cargo套件中，初次只有codegen snapshot因预期的生成源码变化失败。审阅后更新三份快照并单独复跑通过：变更限于C源文件、私有RPC codec头文件的大小／摘要及相应manifest摘要，schema／profile identity、公共头文件和wire未改。其他完整套件目标、Clippy全targets／features拒绝warning，以及正式USB sample构建通过。没有为实现调整合同或错误语义。

两种observer下的链接审计结果相同：

| 工作／资源 | 原A | V |
| --- | ---: | ---: |
| 单字节reader指令／调用 | 52／1 | 19／0 |
| 普通与私有RPC reader正文，各自 | 163 B | 184 B |
| reader entry栈帧 | 48 B | 48 B |
| 规范32 B Chunk解码，指令／调用 | 480／10 | 381／7 |
| 链接text，off | 59616 B | 59664 B |
| 链接text，on | 61872 B | 61920 B |

Chunk静态路径不展开ROM helper体，不含IRQ、窗口异常或调度执行。全镜像共同函数的操作／寄存器／CFG比较中，只有`wlc_getv`和`usb_rpc_wlc_getv`变化；Sample成功发送路径的算法工作没有增加。原非codec对象section记录匹配，生成kernel对象中的私有reader变化单列；on的observer只有`thread_snapshot` literal差异，实际解引用六个字符串，仅`cpu.c`源路径不同。

IRAM、DRAM data、BSS在对应observer模式下均不变：IRAM off／on为45100／46372 B，data为8108 B，BSS为21424／21944 B。六份on采集中，main栈均8192 B、unused 4848 B；两处heap的最大已分配量均52／204 B，A/V相同。

## 三对均同状态，主目标改善没有覆盖保护失败

四份fresh smoke、两份boundary及十二份screen健康检查通过。所有六个整对均为同一boot、解锁、DPMS开、接电且电池Charging，没有状态切换或采样断档；比较分层同时包含锁屏、显示、boot和供电状态。native主线程／worker调度与系统条件持续核对，未renice、改affinity或停服务。没有补对或跨状态拼接。

CPU-on主目标及相关CPU值，单位μs／等效帧或call，r1／r2／r3：

| 指标 | 原A | V | 配对比中位数／稳定性 |
| --- | --- | --- | --- |
| H2D MCU active／等效帧 | 120.094／121.851／120.371 | 114.724／114.863／111.444 | 0.94265，稳定 |
| D2H MCU active／等效帧 | 172.004／171.357／171.778 | 168.203／171.059／170.502 | 0.99258，稳定 |
| 双向MCU active／等效帧 | 167.726／161.623／162.635 | 158.469／156.303／158.247 | 0.96708，稳定 |
| RPC active／call | 428.742／428.657／408.744 | 435.532／439.508／443.245 | 1.02531，稳定、未越5%保护线 |
| Sample active／frame | 167.650／158.789／166.765 | 190.798／190.847／190.188 | 1.14045，A span 5.58%，不可判为稳定 |

等效帧成本为CPU窗口active比例除以同期goodput／32；不是逐帧trace，也不与整段计数相除。host stream CPU另按同期窗口保留为补充观察，未替代预登记的MCU主目标。

所有32 B goodput分量的V/A配对比中位数：

| 分量 | off | on |
| --- | --- | --- |
| 单向H2D | **0.93533，稳定保护失败** | 1.03600，稳定 |
| 单向D2H | 0.99774，稳定 | 1.02674，稳定 |
| 双向H2D | 0.97840，稳定 | 1.00592，稳定 |
| 双向D2H | **0.93371，稳定保护失败** | 1.05719，A span超过5%，不可判定 |

off H2D原A为225542／227443／225638 B/s，V为211008／211098／211046 B/s；off双向D2H原A为102080／100698／102995 B/s，V为95706／94022／94445 B/s。两项均三轮越过95%保护线，不依赖某一轮异常。

## Sample p99是独立、稳定的拒绝依据

on Sample API计时如下，单位μs：

| 指标 | 原A，r1／r2／r3 | V，r1／r2／r3 | 判断 |
| --- | --- | --- | --- |
| p50 | 7.4625／3.7833／7.4625 | 12.0958／12.0958／12.0958 | A不稳定 |
| p99 | 7.4625／7.4625／7.4625 | 18.2708／18.2708／18.2708 | **稳定，V/A 2.44835，保护失败** |
| 包含冷调用的1000次均值 | 7.51027／5.67068／7.51027 | 15.22860／15.22861／15.17303 | A不稳定，保留描述值 |

V的暖调用集中在2903和4385 cycles附近，三轮接近一半一半；首个冷调用均15251 cycles。原A r1／r3为1791 cycles×999、冷13256 cycles×1；r2混有907／908和1791 cycles。完整直方图、前32次和全部原始值保留，不能只以p50跨50%分位作结论。六份Sample的USB IRQ数仍全部为1002。

off Sample暖调用p50／p99均保持2.3167 μs，1000次均值配对比0.99833。observer-off与on呈现不同代价；相同IRQ数和相同Sample算法指令，仍不能限定中断位置、前序驻留与每次事件成本。RPC RTT尾延迟、低频及若干host CPU项也不稳定，全部保留`stable:false`，没有贴成通过。

## 下一判别：分开两个reader落点，不搜索padding

本轮同时观察到确定性解码减量、H2D局部CPU收益与跨路径稳定代价。因此关闭的是这个同时改普通／RPC reader的V，不是否定单字节处理或整个编译期方向。具体cache／总线／调度机制仍未最终归因。

下一步优先做一个限定诊断：**分别只改变普通codec reader、只改变私有RPC reader**，保持其余原A输入，观察H2D收益和Sample／off stream代价分别跟随哪个落点。它比继续扩大内核重构更能区分执行工作与镜像／前序活动交互；不引入永久双引擎、产品开关、IRAM或padding／函数顺序搜索。本轮尚未启动该诊断。

## 证据、源码与清理

- 新重放入口：`benchmarks/notification_progress/varint_product/`。原required-fixed重放及其历史恢复规则未改；新controller在新证据目录明确删除了自动恢复动作。
- 证据根目录：`/home/ww/codings/wirelink-varint-product-20261111/`。关键文件为`final-registration.json`、`compiler-receipt.json`、`object-code-audit.json`、`result.json`、`decision.json`、`handoff.json`及原始captures／状态时间线；采集字段中的B就是本轮V。
- 新分析器五项测试通过；实际重放拒绝错登记顺序、错observer、错误恢复handoff和缺失capture四种篡改。完整数字和不稳定项在结果JSON中。
- V-off／V-on、正式sample和Rust target已永久删除，共1808666079字节逻辑文件量（约1.81 GB），不归档镜像／ELF／对象。删除后仅用原始与文本证据重放，`result-after-cleanup.json`与原结果完全相同。
- 片上保留V-on，BIN身份为`e786546f2a5b11e52c698a4bca94bdcb7d3a082f967fbdc3d5eb0d1675144d8a`。原A参考／必要恢复资产继续保留，未自动刷写。
