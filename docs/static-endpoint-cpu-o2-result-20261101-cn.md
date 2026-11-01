# O2：通知调用减少，但两种候选均未通过性能保护

日期：2026-11-01。O2 本轮完成两种通知收敛候选的正确性验证、实际计数及产品 A/B 筛查，**均不采用，正式源码保持 A**。保留 USB 发布／等待和最后访问的回归测试。接续 [O0/O1 与撤回的 O3](static-endpoint-cpu-optimization-result-20261101-cn.md)。证据目录：`/home/ww/codings/wirelink-cpu-o2-20261101/`。

## 候选的责任划分

正式 USB 仍保留原有 port notify 连接。下表是未采用候选的设计，不是当前默认连接方式。

| 发布源 | 持久条件及清除者 | 候选 wake 责任 | rearm／静默责任 |
| --- | --- | --- | --- |
| RX commit／abort | port RX cursor／claim 和 activity；owner borrow/release／take activity | r2 只用 request 尾部 wake；r3 在成功 commit 后补及时 progress wake | 清 RX_ACTIVE、释放 net_buf、发布 RX_REARM；owner service 才 claim/enqueue |
| TX completion | port completion flags 和 activity；owner tx_take | r2 只用尾部 wake；r3 在 publish 后补及时 progress wake | source lease／DRAINED 不变；callback 最后访问结束后才减 callbacks_active |
| owner release RX | read cursor 和 activity | 当前 owner 链推进，不另给自己 semaphore token | step 后 service，或等待前检查 adapter ready 并继续下一 pass |
| enable／disable | ENABLED、RX_REARM／cancel 状态和 activity | 显式 wake；已有非空 port notifier 已通知时不重复补发 | 首次 arm、disable／恢复不依赖周期 timeout |
| fault／stop／callback 退出 | fault、STOPPING、callbacks_active 和物理 in-flight 状态 | owner 处理同步错误；最后 callback 最终 wake 确认异步进展 | stopped/deinit 仍分别要求 callback、物理 I/O 和 port 静默 |

两个候选均将正式 sample、benchmark 的 port notify 留空，由 adapter 显式负责外层通知；port 仍发布所有原有状态和 activity。保留两个消费者各自的 service／ready-before-wait 连接。未修改 portable core/packet-port API、内核调度、业务工作量、槽数、USB 资源、线程参数或缓存设置，也未引入共享 gate、0→1 通知策略、第二引擎或 fallback。

## 先修复候选中的最后访问问题

第一次实现的生命周期 helper 在 port notify 已经唤醒 owner 后，又读取 adapter 发第二次通知。非空 notifier 若让 owner 在该时刻 deinit，会访问旧 adapter。代码审查发现后停止原块，保留 `qualification-abort.json` 及部分原始记录，另立 r2；原块不混入后续性能结果。

新增负例将旧候选源码与最后访问测试组合：ASan 在 `wl_kernel_usb_bulk_notify` 读取已 poison 的 adapter 时实际 SEGV（`checks-negative.log`）。修复是在发布前记住不变的 notify 连接信息：已有 notifier 时，activity 发布后不再访问 adapter；NULL 连接才显式补发 wake。r3 的 progress helper 只在 callbacks_active 保护下调用。最终两个候选连同这段 helper 都已撤回；保留能抓住该错误的生命周期测试，原 A 实现通过。

## 实际计数证实减少了调用，未证实 CPU 收益

r2/r3 分别制作独立 D-on 诊断构建，运行完整 screen 工作量并逐页核对七个阶段。产品性能构建不含这些探针。计数器、单核快照、phase 边界和 probe 扰动沿用 [O1 工具合同](../benchmarks/notification_progress/hardware/README.md)。

| 计数 | 原 A 路径／先前同源码账本 | r2：全尾部合并 | r3：保留及时 progress wake |
| --- | ---: | ---: | ---: |
| 正常 RPC 通知机会 | 6 | 2 | 4 |
| 正常 Sample TX 通知机会 | 2 | 1 | 2 |
| D 的 RPC 阶段 semaphore give，10000 calls | — | 20002 | 40004 |
| D 的 Sample 阶段 semaphore give，1000 frames | — | 1002 | 2004 |
| D 的 port notify callback | — | 0 | 0 |

阶段计数含 START／STOP 控制流量。r2 的 give 等于 callback_exit；r3 等于 adapter progress notify＋callback_exit。完整计数、业务／字节账本见 `r{2,3}/counts/diagnosis.json` 与原始 JSONL。D 的 owner pass 和空转计数只描述该诊断执行，不作为 A 的原生次数或产品 B/A 的推进收益；通知调用也不是 OS 唤醒次数。

## 两份独立产品块，各三轮 A/B

r2 与 r3 各自预登记 AB／BA／AB、off/on 分别 flash/reset，共 **24 份产品 screen capture**。每份 1000 warmup＋10000 RPC、idle5s、200 次低频 RPC、1000 paced Sample、32 B 三方向 stream；四大小 smoke、编码边界资格验证和最终 A-on smoke 另计。每个块使用一个常驻 controller，逐 native worker 校验 policy0、nice−4、affinity0–5，以及 governor/EPP、USB 拓扑与 MPS。未 renice、改 affinity 或停服务；不与先前 nice16 的块混样。

全部 24 份从 raw 独立重放。每轮值、全部吞吐／host CPU／idle／资源、异常及不可判定项保留在各块 `O2-analysis.json`。这是 O2 筛查，不是 O5 完整 A/B/C，也没有新的 libcsp 分母。

| 候选／指标 | A 三轮原值 | B 三轮原值 | 配对 B/A 中位数／判断 |
| --- | --- | --- | --- |
| r2 on RPC active，µs/call | 427.688／427.608／422.053 | 419.133／413.082／421.134 | 0.98000，下降2.00% |
| r2 off RPC p50，µs | 335.559／335.425／336.749 | 364.091／363.234／363.393 | 1.08291，回退8.29% |
| r2 on Sample active，µs/frame | 167.642／168.379／166.801 | 180.819／180.829／181.374 | 1.07860，回退7.86% |
| r3 on RPC active，µs/call | 420.079／405.472／429.060 | 416.369／407.283／425.879 | 不可判定：A 跨度5.82% |
| r3 off RPC p50，µs | 336.056／337.627／334.274 | 365.188／366.794／365.303 | 1.08669，回退8.67% |
| r3 on Sample active，µs/frame | 166.772／159.283／166.773 | 200.668／200.239／200.651 | 1.20325，回退20.32% |

r2 有八项重复保护失败：off RPC p50/p99、32 B h2d、双向 d2h，以及 on host Sample CPU、Sample API p50/p99、Sample active；八项不可判定。r3 有六项重复保护失败：同四项 off 保护及 on Sample API p99、Sample active；十一项不可判定。两者均有独立、稳定的拒绝依据，不追加采样追求通过。r3 保留及时 progress wake 未恢复延迟，故不能将 r2 的延迟回退单独归因于尾部通知的位置。

两个块共十二份 on 产品记录的 RPC IRQ 均为20002、Sample IRQ 均为1002。相同 IRQ 数和更少 notify 都不足以解释 Sample CPU／API 变化；本轮没有 miss 计数，也没有证据可判定是 ICache、计时或某个独占函数成本。

## 正确性、资源与最终状态

- 两个最终候选各通过 32 位 core/USB 的21个用例，GCC／Clang native64 ASan/UBSan 的 USB 6/6；正式 sample 和 benchmark 固件均构建通过。USB application 的 GCC、Clang ASan/UBSan 单元测试各1/1通过。未修改 WLC，使用 matching 0.13 编译器；benchmark 保持 profile-v2、双角色、4 calls／4 replies、budget16。
- 候选撤回后，保留发布在等待前／真正阻塞后、RX release 早于 RX_REARM、inline TX completion、callback 最后统计访问期间 stop、最终 wake 可 deinit 并 poison、延迟 disable 最后访问等测试。按原 A 的非空 notifier 合同运行，不把候选的 NULL 连接或精确通知次数固化为产品合同。最终 native32/core **20/20**，GCC／Clang native64 ASan/UBSan 各 **5/5**，无 warnings。
- `r{2,3}/O2-resource-audit.json` 对照所查 core、port、native/RPC wire、生成代码和 observer 对象，只有 USB adapter 的代码 section 改变；benchmark 的 port 连接初始化也改变。A 对象已按前次要求删除，本次复用绑定原 A build.json SHA 的 O0 对象记录，而非假称重新读取对象。A/B endpoint4408 B、port88 B、adapter152 B、data/BSS 和 IRAM 不变。r2 `.text` off +32 B／on +16 B，r3 off/on 均 +64 B；未迁入 IRAM或搜索 padding／函数顺序。完整 heap／线程栈观测仍在 raw resource 页中。
- 已精确撤销 adapter/header、正式 sample 和 cross-benchmark backend 的所有产品改动。原 A 是唯一正式路径，新增内容仅为测试和研究记录；不保留实验接口或开关。
- 最后一份 `r3/captures/return-A-on/` smoke 完整通过。板上仍为 `kernel-usb-6b7697df31604f96-wirelink-on`，bin SHA256 `dc129aa2a42f32cb7a9d18e95e426e62f5664ccf1a7905bc7d8e4b1e2ad80275`；所有采集进程、串口与 USB 句柄关闭。

## 下一步：先做低扰动分段定位，再决定 O4

O2 的这两种收敛配置不进入正式实现，也不据此否定静态内核方向。保留 A，不再扩大合并范围或枚举 wake／布局组合。下一步优先定位 Sample 与 observer／推进边界的交互：在独立诊断构建中先减少探针覆盖，量化外层 service/step、callback/enqueue 的范围及探针扰动，再决定是否实施 O4。计时继续放在 portable core 外，不相加重叠的 inclusive scope，不以当前探针固件替代产品成绩。O4/O5 尚未执行，120%目标未作新判定。


## 清理与重放

已按要求永久删除本轮所有无用固件／host 二进制、诊断及 negative 构建、sample 构建、软件检查树和其缓存，没有另存编译产物或镜像归档。`cleanup.json` 记录删除22601个文件／链接；源代码、源补丁、原始采集、注册、配置及测试／机器码记录保留。当前需要的 A 固件、gated host 和 matching WLC 位于先前的基线目录，未删除。

删除后，r2/r3 的24份产品 raw capture 和两份最终诊断 raw capture 均重新校验并重放，结果不变。候选历史 build.json 仍记录当时固件身份，但镜像已删除，不能再完整验证候选 build bundle；以后重建需重新登记。原始重放命令为：

```sh
python -B /home/ww/codings/wirelink-cpu-o2-20261101/r3/tools/replay.py \
  --evidence /home/ww/codings/wirelink-cpu-o2-20261101/r3 \
  --out /absolute/new-result.json
```

r2 使用相同目录结构。输出路径必须不存在；重放不重建已删除的编译缓存。
