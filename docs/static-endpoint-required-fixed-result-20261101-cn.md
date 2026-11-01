# Required-fixed 编码重构：机器码减少，尚不升为基线

## 结论与当前可用版本

本轮完成了一个限定候选：**Schema-v1 全 required、定宽 scalar 的编码器只做一次预检，然后按固定偏移写出。** 合同测试、完整 WLC 检查、ESP32-S3 构建和实际机器码检查通过。Sample 成功路径从337条业务指令、14次调用降到238条、9次调用。

无新增探针的三对 A/B 已执行完，但每个 observer 只有前两对满足同状态条件：第三对 off 跨越锁屏状态，第三对 on 有主机采样断档。前两对同时显示 Sample 加快和部分 stream 吞吐下降。**按预注册规则，本轮结果未决，不接受为产品新基线，也不追加测量或放宽保护线。**

- 当前 WLC `main` 为 `df021549187deb59051ddd32b949ea679bf5b98c`：保留新增合同测试，生成器仍为原实现。
- 候选源码单独保留在 WLC 本地分支 `experiment/required-fixed-20261101`，提交 `8ac90a9f5289eda0896408babb7b83771a64d142`。该分支包含合同测试和优化；未合入 `main`。
- 板子已恢复原 A-on，回刷后的 smoke 完成且原始数据复算通过。恢复资产仍为 `/home/ww/codings/wirelink-cpu-optimization-20261101/A-on/`，未替换匹配的原 WLC。

## 候选只消除可由 schema 确定的工作

改动位于 WLC `src/codegen/fixed.rs`，沿用现有非空、最多8字段的 scalar shape 入口。只有 Schema-v1、全部 required、所选编码宽度恒定的 fixed32／fixed64／float32／float64 使用新路径；uint32 varint、optional、ManagedTagged 和 positional 保持原路径。

新路径依次检查 value、required presence、输出／length 指针和容量，全部通过后按常量位置写 key 和大端值字节，最后写常量 length。它不再调用 measure 或 put32／put64，不再重复 presence 检查或维护 cursor。公开 measure／encoded_size、decode、fingerprint 和 shape 共享入口仍保留；没有新增平台属性、编译选项、堆分配或运行时模式。

先在原生成器上固定错误优先级、失败不写输出／length、容量边界与 wire 行为，再执行候选。新增 C 差分测试覆盖16种 presence 组合、0..40容量、NULL 参数组合、非对齐输出、20 B Sample golden、多字节 key、共享 shape、128组整数／浮点位模式（含负零和 NaN payload）及编解码往返；GCC／Clang 均使用严格 C11、`-Os -Werror` 和 ASan／UBSan 通过。候选完整 `cargo test --locked --no-fail-fast`、Clippy 全 targets／features 拒绝 warnings、off/on 基准固件及正式 USB sample 构建通过，既有快照无需更新。

本基准实际改变了 `usb.c` 中 SetpointRequest、Sample、Progress、ControlRequest 四个符合条件的编码 shape，并非只改 Sample。生成文件中只有 `usb.c` 及记录其内容身份的 `usb_kernel_manifest.json` 变化；其余头文件、values、kernel 和 deployment 文件相同。

## 实际 Xtensa 机器码验证了减量，也保留了布局变化

输入使用原 A 的冻结产品／应用／observer 源码及同一 Zephyr／SDK，不使用当前 dirty benchmark 工作区。A/B 在每个 observer 模式下共用同一个保留的 A host，仅严格指定各自 device identity。主机 workload、USB预算、编译策略和 observer 源码未改。

| 标注的正常 Sample 路径 | 原 A | 候选 B |
| --- | ---: | ---: |
| 业务静态执行指令数 | 337 | 238 |
| 业务调用数，含 main→send、不含读钟 | 14 | 9 |
| main 之后峰值 CALL8 深度 | 9 | 8 |
| shape 编码函数体 | 158 B | 186 B |
| 成功路径＋读钟括号＋literal 行，off／on | 40／37 | 36／35 |
| 链接 `.text`，off | 59616 B | 59584 B |
| 链接 `.text`，on | 61872 B | 61840 B |

候选 shape 的成功路径为67条指令、零调用。IRAM、data、BSS 大小在对应 off/on 中不变。函数体本身变大28 B，而整条路径工作与链接 text 减少，不能只按单个函数尺寸评价这次改动。

原 A 的对象已在先前清理中删除；对象检查使用与 A `build.json` 绑定的 O0 section 记录，不声称重读了 A 对象。候选 core、packet、wire、USB adapter、生成 kernel、原 observer 的相应 text／literal sections 与该记录一致，除预期变化的 `usb.c` 和 on 的 `.literal.thread_snapshot`。后者另从实际 A/B ELF 解引用了六个字符串，只有 `cpu.c` 源码路径不同；记录见 `observer-string-referents.json`。

这些是标注成功路径的结构计数，不包括 IRQ、窗口异常或抢占执行。指令减少、调用变浅和链接搬迁同时发生，不能据此把后面的耗时变化分配为窗口异常成本或某条 cache miss。

## 三对 screen 的状态约束未能全部满足

预注册位于证据目录 `final-registration.json`。四组 fresh smoke、A/B boundary qualification 先完成，再按每个 observer 的 AB／BA／AB 顺序运行三对。每份 screen 保留原1000 warmup、10000 RPC、idle、低频 RPC、全部1000次 paced Sample 和32 B三方向 stream；未添加逐调用探针。

native worker 的调度签名在 warmup 前冻结并在运行中持续核对；1 Hz sidecar 记录图形、电源与时间状态，整个 A/B 对（包括两份之间的间隔）共同判定。出现状态切换或采样缺口时，整对保留但不纳入同状态比较，不补对、不跨状态拼接。

| 对 | off | on |
| --- | --- | --- |
| r1，AB | 解锁，合格 | 解锁，合格 |
| r2，BA | 解锁，合格 | 解锁，合格 |
| r3，AB | lock 状态未知／变化，剔除 | 采样间隔超限，剔除 |

on 的断档错误是 `host sampling gap or suspend`；记录不足以把它进一步定为实际系统待机。十二份业务采集均完成、健康检查通过，数据没有因状态剔除而删除。原预注册要求同状态三对且 A/B 各自 span≤5%（idle 为0.5个百分点），因此本轮没有满足稳定判定的比较。

## 前两对：Sample 收益明显，stream 代价需要先处理

下表只描述两对合格数据，不作三对稳定验收；完整逐份数值、直方图和指标在 `result-with-low.json`。

| Sample API，μs／次 | A-off | B-off | A-on | B-on |
| --- | ---: | ---: | ---: | ---: |
| p50，两对均相同 | 2.3167 | 1.6083 | 7.4625 | 3.9500 |
| p99，两对均相同 | 2.3167 | 1.6083 | 7.4625 | 3.9500 |
| 包括冷调用的1000次均值，两对均相同 | 2.371146 | 1.657221 | 7.510267 | 3.996817 |

全量均值的配对降幅为 off 30.11%、on 46.78%，不是仅看 p50 跨越50%分位边界。每份 off 的完整分布为 A：13631 cycles×1、556×999；B：12119×1、386×999。前两份 on 的完整分布为 A：13256×1、1790×1、1791×998；B：11759×1、1373×1、948×998。第三对的这些分布也保留，但不用于补足同状态样本数。

on 的 Sample 阶段每次 active 从166.772／168.267 μs降为152.705／152.723 μs；这两对 A/B 的 USB IRQ 均为1002次。次数相同仍不约束 IRQ 的位置、持续时间和前序驻留。

| 吞吐，kB/s（十进制），r1／r2 | A | B | 两个配对 B/A 的中位数 |
| --- | --- | --- | ---: |
| off，单向 H2D | 239.514／239.904 | 202.259／188.301 | 0.815 |
| off，单向 D2H | 216.672／215.603 | 223.014／215.642 | 1.015 |
| off，双向 H2D | 148.013／147.040 | 143.219／143.149 | 0.971 |
| off，双向 D2H | 108.077／104.877 | 85.818／84.019 | 0.798 |
| on，单向 H2D | 204.378／199.142 | 182.336／181.978 | 0.903 |
| on，单向 D2H | 158.016／158.138 | 159.885／158.464 | 1.007 |
| on，双向 H2D | 124.397／125.357 | 118.726／117.536 | 0.946 |
| on，双向 D2H | 68.806／71.238 | 60.371／60.243 | 0.861 |

单向 H2D、双向 D2H 的四组比较都在两对中出现超过5%的损失。host Sample CPU 的两对比值中位数也为 off +6.81%、on +5.10%；不是所有保护指标都改善。RPC 与其他 CPU／idle 指标的完整原始系列同样保留，没有因 Sample 加快而删掉保护项。

低频 RTT p50 的配对比中位数为 off 0.990、on 0.996。其 p99 参考 A 在前两对间的 span 已为 off 14.19%、on 24.14%，因此即使忽略第三对缺失，也不能将低频尾延迟写成稳定改善；低频 CPU 同样保留逐份数据和稳定性判断。

这些损失尚未构成预注册所需的三对稳定拒绝证据，却足以阻止当前候选直接升为基线。下一步应先利用保留的 A/B 机器码检查 **Progress 编码及 stream 推进路径的变化**，再决定是否值得单独立项判别；不继续叠加 kernel、通知或布局改动。

本轮表明减少确定性编码工作值得尝试，但没有消除指令驻留与前序活动的交互。Sample 改善不能证明原波动根因，也不能覆盖 stream 的代价。

## 位置、复算与清理

证据根目录：`/home/ww/codings/wirelink-required-fixed-20261101/`。

- `inputs/`、源码补丁、`generated-candidate/`、`source-receipts.json`：冻结输入与候选身份。
- `object-code-audit.json`、`B-{off,on}.asm`、`text-build-evidence/`：已测机器码、map、配置、编译命令与 section 记录。
- `captures/`、环境／worker／host-state 时间线、registration、逐对 state 文件：原始测量与资格判定。
- `result-with-low.json`、`decision.json`、`replay-checks.json`、`cleanup.json`：含低频保护项的完整复算、决定、工具检查和清理记录。`result.json` 为此前同数据的首版派生结果。

复算入口在 `benchmarks/notification_progress/fixed_encode/`。新增六项保护规则单元测试、四项既有机器码分析器测试，以及对不完整恢复、注册顺序、资格标记、原始数据改动的四项拒绝检查通过。完整复算包含六份 qualification、十二份 screen 和最终 A-on smoke。

本轮 B-off／B-on／正式 sample 构建目录和 Rust `cargo-target` 已永久删除，共1778049095字节逻辑文件量（约1.78 GB），未归档镜像、ELF、对象或编译器缓存。当前仍需使用的原 A 和匹配编译器保留；原始 UART 二进制日志属于测量数据，不是固件镜像。删除后再次仅用文本／原始数据复算，结果与删除前完全一致：

```sh
python -B benchmarks/notification_progress/fixed_encode/analyze.py \
  --evidence /home/ww/codings/wirelink-required-fixed-20261101 \
  --out /absolute/new-replay.json
```

本轮止于单候选、32 B product A/B screen；完整 O5 A/B/C 验收未执行，当前版本与原 A 基线的接受状态不变。
