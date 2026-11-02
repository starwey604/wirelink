# H2D 接收链检查：查找未走，单字节仍调用 64 位移位 helper

## 下一候选选单字节 varint，不扩大解码器或通知重构

沿[五字段计数结果](static-endpoint-owner-local-result-20261102-cn.md)检查原产品 A 和 required-fixed B 的实际机器码后，下一项推荐收缩为：**从原 A 独立派生，只给通用 `getv` 增加单字节成功返回，保留原多字节路径。** 本轮完成离线检查和复算工具，未修改生成器、构建产品固件或访问板卡。

决定依据：

1. 正常 32 B Chunk 的两个字段都命中现有顺序游标，`wlc_find_field` 和 `wlc_skip` 均不执行；继续优化字段查找不减少这条正常路径的工作。
2. 两个 key（`0x0d`、`0x12`）和一个 length（`0x20`）都是单字节，但每次 `wlc_getv` 仍执行52条已链接指令，并调用一次 ROM `__ashldi3`。每帧合计三次。
3. A/B 的这条成功解码路径均为480条指令、10次调用，排除被调用的 ROM 函数体；B 没有增加这份固定工作。因而它是可确定的减量目标，**不是已查明的 B 回退原因**。

这比完整 schema 专用解码器更小：不必重新实现乱序／未知字段、duplicate、required 与失败后输出状态，也不改 core／adapter 推进责任。不能直接把 key reader 换成 `getv32` 来省64位工作；过宽 key 的错误优先级和游标状态仍应按原 reader 保留。

## 实际 H2D 调用链和计数边界

冻结的生成 `usb_kernel.c` 在 `case 103` 调用 `stream_chunk_decode`，成功后调用 `on_message_stream_chunk`。设备 backend 将它绑定到 `on_chunk`，再调用 benchmark `receive`。上游是 core 的接收、native-none 拆帧和 route；设备 `measure／emit` 属于 D2H，不是这里每帧的工作。

规范输入体长39 B：key 1 B＋sequence 4 B＋key 1 B＋length 1 B＋payload 32 B；native 帧长43 B。序号四字节不是 varint，不随序号大小改变下表。

| 标注路径 | 每帧次数 | 每次指令数，不展开调用 | 每次调用 |
| --- | ---: | ---: | --- |
| stream_chunk_decode | 1 | 9 | wlc_decode ×1 |
| wlc_decode | 1 | 171 | clear ×1、getv ×2、read_value ×2 |
| wlc_clear | 1 | 48 | 无 |
| wlc_getv，单字节 | 3 | 52 | ROM __ashldi3 ×1 |
| wlc_read_value，fixed32 | 1 | 59 | 无；硬件 LOOP 读四字节 |
| wlc_read_value，bytes | 1 | 37 | getv ×1 |
| 合计，含各层 call／return | — | **480** | **7次镜像内调用＋3次ROM调用** |

以上在 A-off、B-off、A-on、B-on 四份实际反汇编中相同。`wlc_decode` 的171条包含两次字段处理和两项末尾 required 检查；`wlc_clear` 遍历两项描述符。指令数按明确分支选择和循环次数重建，不是退休指令 trace 或 cycles；ROM helper 函数体、route／backend／core、IRQ、抢占和寄存器窗口异常均不在480内。三个 `getv` 合计156条，占所列镜像内指令的32.5%，不能把它全部当成未来可省工作或吞吐收益。

独立 host C 路径计数直接包含冻结 A 的生成源码，在临时副本内加入计数；GCC／Clang 均验证解码值、presence、长度和借用指针：

| 输入 | getv调用 | varint字节 | find_field调用 | skip调用 |
| --- | ---: | ---: | ---: | ---: |
| 顺序32 B | 3 | 3 | 0 | 0 |
| 乱序32 B | 3 | 3 | 2 | 0 |
| 末尾加未知varint字段 | 5 | 5 | 1 | 1 |
| 顺序128 B，两字节length | 3 | 4 | 0 | 0 |

四例均执行一次 clear、两次 read_value。host 计数核对源码控制流，Xtensa 指令数来自原产品反汇编；二者不混作板上新增测量。单字节早返回不能取消后面三种情况所需的语义。

## A/B 差异主要仍是搬迁，描述符没有搬家

两种 observer 下，核对的13个接收链函数均保持相同的操作／寄存器／控制流及 literal 指向的符号或数值。不是比较函数长度后推断代码相同。`wlc_read_value` 的180→179 B只少一字节 padding，成功执行指令不变。

CPU-on 中的具体地址：

| 函数 | 原 A | required-fixed B |
| --- | --- | --- |
| usb_kernel_route | 0x4200c6e4 | 0x4200c6e4 |
| stream_chunk_decode | 0x4200da24 | 0x4200d8f4 |
| wlc_decode | 0x4200d3a8 | 0x4200d278 |
| wlc_read_value | 0x4200d2f4 | 0x4200d1c4 |
| wlc_clear | 0x4200ee24 | 0x4200ecf4 |
| wlc_getv | 0x4200d0b4 | 0x4200d0b4 |
| on_chunk | 0x42001dd4 | 0x42001dd4 |
| benchmark receive | 0x42000e80 | 0x42000e80 |

多个 helper 搬了−304 B，但 `getv` 本身、其 `__ashldi3` ROM 目标 `0x400021b4` 及 literal slot 不变。wrapper 的 descriptor literal、decode 的 `0xfffe` 常量 literal 的 slot 也在对应 A/B 中不变。

`stream_chunk_fields` 始终位于 `0x3c0100d0`、128 B，`stream_chunk_desc` 位于 `0x3c010cd4`、12 B；生成定义和 value/view 头文件一致。这来自保留的符号表与源码，不声称重读已删除 B ELF 的数据字节。描述符在 DROM，不能把它们记为 ICache 指令行。

仅所列成功解码路径，按完整指令字节和 literal slot 去重的32 B行数：off A/B为31／30，on为29／29。其中 literal 均为两行。on 的 wrapper 从一行跨到两行，read_value 两支合并范围从七行变为六行，总量并未增加。**行数未增不能排除与其他函数的组冲突、前序驻留或总线等待**；这些集合不是运行中访问顺序，也不包含整个 owner／USB／observer 工作集。现有证据仍不能给回退分配具体 miss 或调度成本。

## 后续业务成本保留，不靠删 benchmark 工作提速

实际 `on_chunk` 每次先清零 `0x424`＝1060 B的 `ub_message_t`，再复制32 B payload，最后经 receiver 回调。其栈帧为 `0x450`＝1104 B；对应 A/B 相同。

benchmark `receive` 仍检查 active／phase／序号／长度／payload，累计字节，读取 uptime，维护窗口字节与每1024 B的 Progress pending。机器码含 `z_impl_k_uptime_ticks` 和 ROM `__udivdi3` 调用；payload 校验 `ub_verify_chunk` 的操作也相同，但该函数地址搬了−304 B。这些调用不能混入“codec独占成本”。不删除清零、复制、pattern校验或计时账本来获得库优化成绩。

## 下一阶段的最小验证顺序

1. 从原 A 的生成器派生独立候选 V，不叠加未接受的 required-fixed B。只处理 `wlc/src/codegen/templates/common.c.in` 的通用 `getv` 单字节返回；多字节、截断、溢出和非规范编码继续走原算法，进入该算法前不推进游标。
2. 先固定原 reader 为差分参考，验证所有单字节、规范2–10字节、非规范／截断／溢出、非零起始offset和边界，逐项比较 status、最终at、输出值及失败时输出状态；跑受影响的生成器合同测试、完整 WLC 测试／lint。
3. 构建原产品无新计数的 off／on，审计真实单字节路径是否消除 ROM 移位调用，以及其他路径、资源和链接变化；没有实际机器码减量就不进板上测量。不改布局选项、IRAM、驱动或调度参数。
4. 按既有 smoke／boundary 和预登记同状态配对规则测 A/V，保留RPC、低频、Sample、三个stream的所有分量、CPU／延迟／资源保护项。用未加探针产品结果决定是否保留，不从156条静态工作推测百分比，也不以H2D改善覆盖D2H损失。

这一步回答“能否稳定减少固定成本”，与原 A/B 回退的最终机制归因分别报告。

## 交付与复算

- 工具：`benchmarks/notification_progress/a_residency/inspect_receive.py`、`receive_census.c`、`test_receive.py`。
- 证据：`/home/ww/codings/wirelink-receive-inspection-20261102/result.json`，包含四份机器码路径、各指令、调用／literal／行集合、13函数比较、描述符源码与符号位置，以及两个编译器的四例源码计数。
- 新增四项 walker／调用解析测试、本目录全部18项测试通过；测试及实际证据复算记录见同目录 `checks.txt`。C harness 使用严格 C11、`-O2 -Wall -Wextra -Wpedantic -Werror`，临时源码／可执行文件随退出永久删除，没有新增固件、镜像、Rust target或保留构建缓存。
- 板卡仍为上一轮最后健康的 **`owner-local-20261102-B-on` 诊断固件**；本轮没有刷写、恢复或额外收尾smoke，接受的产品／科学基线未变。身份继续沿用上一轮 `handoff.json`。

复算命令见工具目录 README。后续实施者可以直接以本轮 V 边界开始，不需要再做通知计数或重建 B 才能继续。
