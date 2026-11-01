# Required-fixed 后续检查：Progress 指令减少，owner 单位计时增加

## 下一步优先查 owner 推进／重试频率，不继续扩大重构

本轮只检查[上一轮候选](static-endpoint-required-fixed-result-20261101-cn.md)保留的实际 A/B 反汇编、冻结源码与原始 stream 数据，没有重新构建、刷板或添加探针。检查将问题缩小为两类：**同样的 owner 代码执行了更多次，或每次执行变贵。**

三个直接结果决定了这个顺序：

1. Progress 成功编码的静态执行工作从158条指令、5次调用降到66条、零调用，没有增加固定业务工作。
2. 现有 USB 字节账本能够反推出成功发送的普通 Progress 数量。A/B 均维持约每32个数据帧一份，未见成功 ACK 密度放大。
3. CPU-on 的 H2D owner inclusive-IRQ 计时从每等效数据帧67.73／69.99 μs增至88.54／88.74 μs；相应 owner 函数的操作与控制流结构没有增加。

因此先查 **service／step 调用次数、Progress admission 尝试与 WOULD_BLOCK 次数、无有效推进的 pass** 比继续简化 Progress 成功编码更有判别力。当前数据仍不能在执行频率、指令驻留／总线等待、调度相位之间完成最终归因。原候选保持未决、未合入，默认生成器和原 A-on 基线不变。

## Stream 中真正经过的 codec 与推进路径

冻结设备 `usb/device/main.c` 每轮先执行 `ub_backend_service`：adapter service → kernel step → adapter service。普通 stream 分支依次处理待发 Progress、结束标记、可发 Chunk；Progress 成功入队后才清 pending。`UB_PROGRESS_BYTES=1024`，`UB_WINDOW_BYTES=2048`，32 B数据对应约32帧反馈一次、64帧的业务发送窗口。只有一个普通 message snapshot，Progress 与 Chunk 共用它；失败入队后重试次数没有原生导出。

主机真实输入是保留 A 的 `A-on/sources/host-main.cpp`（screen-gated harness），而非当前 benchmark 工作区。其 stream 循环同样先回 Progress，再发送 DRAIN，最后发送窗口允许的 H2D Chunk；阶段末读取 STOP 和 CPU_STATS。本轮核对了这一实际 harness 的次序，未用未加 gate 的源码代替执行事实。

生成 `usb_kernel.c` 显示：

- 普通 Progress：`usb_kernel_send_progress_view → wl_kernel_send → encode_frame → usb_kernel_encode_progress_view → progress_encode → wlc_scalar_shape_2_encode`。
- 普通 Chunk：对应 stream_chunk view 入口，最终进入 `stream_chunk_encode → wlc_encode`。其 descriptor 只有 fixed32 sequence 和 bytes data，不经过新的 required-fixed shape。
- Setpoint／Control RPC 使用 `usb_kernel_codecs.h` 中独立的 ManagedTagged private codec，该头文件没有改变。`usb.c` 中虽有四个 shape 的源码变化，但本次链接的普通编码 shape 只有 Sample 与 Progress；不能把 RPC 时间变化直接归因于 ControlRequest 编码被优化。

32 B Chunk 的实际 native frame 长43 B，Progress 长23 B，两者 wire 长度在 A/B 中不变。

## Progress 减少了调用；Chunk 路径只有一处不执行的密度编码变化

按有效指针、三个 required presence 均为 true、容量至少19 B标注 Progress 成功路径，off/on 得到相同的工作计数：

| 范围 | A | B |
| --- | ---: | ---: |
| shape 函数体 | 142 B | 184 B |
| shape 自身成功指令 | 54 | 66 |
| 加入实际 helper 执行次数后的指令 | 158 | 66 |
| helper 调用 | 5 | 0 |

A 的五次 helper 是 measure 一次和 put32 四次：fixed64 的高／低字也各执行一次 put32，不能按 C 源码里的 put64 名字少计。B 直接写固定偏移。上述计数不含 IRQ、窗口异常或抢占执行。

比较保留反汇编与原符号大小后，off/on 分别解析890／925个共同函数。除两个实际变化的 shape 外，只有通用 `wlc_encode` 存在操作助记符差异：

```text
A: l32i.n a2, a9, 0      B: l32i a2, a9, 0
A: mov.n   a10, a1       B: or    a10, a1, a1
```

这两条位于 `kind == WLC_F64(13)` 的高字写出分支，指令条数没有增加。StreamChunk 的两种 kind 为 F32(12) 和 BYTES(16)，不执行该分支。通用 encoder 总函数体在 A/B 都为347 B。`wlc_measure` 的272→270 B、`wlc_read_value` 的180→179 B是内部 padding 差别；去掉 alignment 字节、按目标指令序号核对分支后，操作与控制流一致。

`ub_backend_service/send/wait`、`wl_kernel_step/send`、packet／USB adapter、Chunk／Progress 接收回调和生成 route 的操作／控制流也没有增量。分析器分别记录 L32R referent：相同操作结构不等于数据引用相同；例如 service 的错误恢复打印字符串地址有变化，不会被抹成“整个函数完全相同”。

## 已发生的搬迁不小于 −32 B 总 text 所暗示的程度

虽然候选总 text 只减少32 B，函数位置并非都只平移32 B。A-on 的若干实际入口如下：

| 函数 | A-on | B-on | 位移 |
| --- | --- | --- | ---: |
| Progress shape encode | 0x4200d264 | 0x4200eeb0 | +7244 B |
| stream_chunk_encode | 0x4200da0c | 0x4200d8dc | −304 B |
| 通用 wlc_encode | 0x4200d884 | 0x4200d754 | −304 B |
| 通用 wlc_measure | 0x4200ef90 | 0x4200ef68 | −40 B |
| wlc_put32 | 0x4200ee84 | 0x4200ed54 | −304 B |
| native seal | 0x4200ec34 | 0x4200eb04 | −304 B |
| wl_kernel_step | 0x4200ac18 | 0x4200ac18 | 0 |
| USB adapter service | 0x4200bd58 | 0x4200bd58 | 0 |

off 的通用 codec／seal 同样移动−304 B，Progress shape 移动+7224 B。编码结构改变同时带来了函数重排和不同的行边界，因此“core 没改、text 变小”不足以排除单位执行成本变化。这里只记录实际地址，不据静态位置认定某组 cache 已经冲突。

## 从既有字节账本求出成功 Progress，不需要新探针

对每段 `host_health(stream_begin)` 到 `stream_end`，主机的完成字节数包含三次 Control 交换（START、STOP、CPU_STATS）、Chunk、普通 Progress，以及各方向一个 DRAIN／END marker。按冻结协议逐项扣除：

```text
host 普通 Progress 数 = (host TX增量 − 三个请求 − 43×sent_chunks − 23×DRAIN) / 23
device 普通 Progress 数 = (host RX增量 − 三个响应 − 43×received_chunks − 23×END) / 23
```

Control 仍为 tagged varuint，不能按 `usb.c` 中的全 fixed32 编码估算。1000 warmup＋10000 RPC 后，operation 落在8192..1048575，metadata 为 session8＋operation3字节；native prefix4字节。command ID 为五字节 varuint，所有参数（含零值）均带 key。响应还计入真实 JSON 的字节数及其 varuint 长度；工具核对原始 JSON 拼写与长度模型相同。

全部36个 stream 的两个方向余数均非负且恰好被23整除。下表只列同状态合格的前两对、包含准备／窗口／drain 的**整段**成功数，不能当作5 s窗口内的调用／重试计数：

| 数据方向与 observer | A数据帧，r1／r2 | A普通反馈，r1／r2 | B数据帧，r1／r2 | B普通反馈，r1／r2 |
| --- | --- | --- | --- | --- |
| H2D，off | 44964／44914 | 1406／1404 | 38045／35335 | 1186／1102 |
| H2D，on | 38280／37411 | 1195／1167 | 34058／34132 | 1062／1064 |
| 双向中的H2D，on | 23354／23547 | 719／726 | 22249／22034 | 685／678 |
| 双向中的D2H，on | 12900／13339 | 403／417 | 11338／11307 | 355／354 |

反馈对应反方向的 Progress。约32帧的比例在 A/B 中保持；尾部不足1024 B的 flush 和 pending 合并允许小幅偏离。成功计数没有放大，**失败 admission／WOULD_BLOCK、wake 和空 service 频率仍未知**，字节账本不能恢复这些值。

## 增量主要出现在每等效帧的 owner inclusive 计时

下面只使用前两对合格的 CPU-on 数据，单位为 μs／等效数据帧，r1／r2。等效帧率取同阶段 `(H2D goodput + D2H goodput)/32`；CPU 窗口耗时除以该帧率折算的帧数。双向两端窗口边界分别计量，这是窗口归一化，不是逐帧 trace。

| 方向／计时范围 | A | B |
| --- | --- | --- |
| H2D active | 120.62／123.65 | 146.02／146.44 |
| H2D owner inclusive IRQ | 67.73／69.99 | 88.54／88.74 |
| H2D USBD inclusive IRQ | 16.82／16.80 | 17.34／17.72 |
| H2D UDC inclusive IRQ | 20.68／21.38 | 22.67／22.53 |
| H2D USB IRQ span inclusive | 11.58／11.68 | 13.89／13.87 |
| 双向 active | 164.28／161.50 | 178.17／179.40 |
| 双向 owner inclusive IRQ | 90.84／89.57 | 102.47／103.67 |
| 单向D2H owner inclusive IRQ | 101.90／100.77 | 110.43／110.79 |
| 单向D2H USBD inclusive IRQ | 20.57／20.59 | 16.26／16.33 |

H2D owner 两对分别增加30.72%／26.79%；双向 owner 增加12.81%／15.74%。H2D IRQ／等效帧从1.0312／1.0311变为1.0276／1.0280，未随单位 CPU 成本增加。单向 D2H 的 owner 增加而 USBD 降低，也解释了为何单看总 goodput 接近不变会漏掉内部成本变化。

双向 MCU active 已从99.18%／99.22%升至99.72%／99.67%，与吞吐受 CPU 预算约束的解释相容。owner、USBD、UDC、IRQ 的 inclusive 范围重叠，不能相加或相减得到“exclusive 编码／框架成本”。只有两对合格数据，本轮保持描述性定位，不补成稳定验收。

## 推荐的下一判别与本轮交付

首选下一项是独立诊断 A/B 的**阶段总量计数**：对 owner loop／adapter service／kernel step、Progress 和 Chunk 的 send 尝试／WOULD_BLOCK、TX take／submit／completion 记录次数，只在阶段末导出。成功 Chunk 和本轮反推的 Progress 数作为分母，直接回答是否出现了推进／重试放大。

- 若每成功帧的 pass／尝试次数明显增加，先沿唤醒、单普通 snapshot 与 Progress 优先级找放大点，不改编码字节或业务窗口。
- 若次数接近而单位计时仍增加，再把重点放回相同代码的执行成本、驻留及调度交互，而不是继续删成功路径指令。

诊断计数会改变程序布局与时序，应用它检验计数假设，不能替代未加探针产品 A/B 的时间收益。本轮没有启动这个新硬件实验，也没有新增需要清理的固件、构建缓存或镜像。

新增工具与六项针对性测试位于 `benchmarks/notification_progress/a_residency/inspect_stream.py`、`test_stream.py`。证据在 `/home/ww/codings/wirelink-required-fixed-20261101/stream-inspection/result-checked.json`，保留全部函数比较、Progress 成功路径、36份 stream 的计数与 CPU 归一化值和原资格标记。旧的12份 screen、六份 qualification 与最终 A-on smoke 已由既有入口重放通过；未访问串口、USB或调试器。

复算命令见该目录 README。工具仅依赖保留的文本／原始数据及 Python 环境中的 pyelftools，不依赖已删除的候选 ELF 或编译器。
