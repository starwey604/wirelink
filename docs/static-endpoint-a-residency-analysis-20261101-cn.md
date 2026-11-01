# 原 A 的 Sample 机器码与指令驻留候选

## 优先查调用之间的驻留，不改读钟或窗口向量

直接分析保留的A-off/on ELF后，优先方向是：**Sample编码桥接入口、编码叶函数及literal行，与两次调用之间的owner推进／USB／周期observer路径的驻留交互。** 依据有三点：

1. A-off/on的11个Sample业务函数，经重定位归一化后指令结构全部一致；正常成功路径都是337条业务指令、14次业务CALL8／CALLX8，没有直接调用observer。
2. 原读钟函数和六个窗口异常向量已经在片内IRAM。窗口向量在两个ELF中地址和指令字节完全相同，不是IROM取指miss的来源。
3. CPU-on正常成功路径的32 B行足迹反而更小：含读钟边界及literal槽，off为40行、on为37行。编码链自身变大、单独装不进16 KiB，不是这两份ELF给出的解释。

这些事实降低了“新增API指令工作”和“窗口向量自身被ICache挤出”的优先级。原A的耗时双档仍需要运行时驻留／PC证据；本轮不新增硬件成绩，不把静态地址冲突候选写成已发生的miss。

## 使用的是已测 A，不是重新链接的近似版本

输入为 `/home/ww/codings/wirelink-cpu-optimization-20261101/A-{off,on}/` 的原ELF、map、构建配置和冻结源码。分析入口将实际ELF与原`build.json`中的固件身份记录绑定，未重新编译、链接、刷板或连接调试器。

输出在 `/home/ww/codings/wirelink-a-residency-20261101/final/`，包含完整反汇编和`result.json`。辅助证据根目录保留符号表、从原A归档读取的应用／observer／DWC2源码、官方手册相关节选及检查记录。没有将当前dirty benchmark源码当作原A证据。

构建配置为240 MHz、16 KiB／8路／32 B ICache、`-Os`、LTO off、非SMP、无PSRAM。实际启动函数`esp_config_instruction_cache_mode`也向`Cache_Set_ICache_Mode`传入`0,1,1`，对应本地SDK的16 KiB、8路、32 B枚举。Xtensa `core-isa.h`里的`XCHAL_ICACHE_SIZE=0`描述的是CPU核内cache配置，不能据此否定芯片外接的SoC ICache。IROM地址范围`0x42000000..0x43ffffff`通过ICache访问，见[ESP32-S3 TRM 4.3.3](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf)。

## 还原计时括号、固定业务路径和回调绑定

A-on的原计时位置为：

```text
0x4200114e  L32R clock pointer
0x42001151  CALLX8 sys_clock_cycle_get_32
0x42001154  save start
0x42001159  CALL8 send_message
0x4200115c  save status
0x4200115e  L32R clock pointer
0x42001161  CALLX8 sys_clock_cycle_get_32
```

A-off对应`0x4200107a..0x4200108d`。两个括号的归一化指令相同。整个`main`并非字节／指令全相同：启动打印的observer参数是0／1，字符串引用地址也不同；这些差异不在上述括号中。

正常成功路径如下；`measure`和`put32`不是先后嵌套的两层，而是同一shape函数分别调用的叶函数：

```text
main → send_message → ub_backend_send → usb_kernel_send_sample_view
     → wl_kernel_send → encode_frame → usb_kernel_encode_sample_view
     → sample_encode → wlc_scalar_shape_1_encode
                         ├─ wlc_scalar_shape_1_wlc_detail_measure ×1
                         └─ wlc_put32 ×4
encode_frame → wl_kernel_native_none_seal
```

`usb_kernel_send_sample_view`加载固定编码回调：off的literal槽`0x4200081c`指向`0x4200bca8`，on的`0x420008f0`指向`0x4200c560`。参数经生成send的a13、`wl_kernel_send`的a5／a14传到`encode_frame`的a6，再执行`CALLX8 a6`。这条Sample路径不是运行时查找通用codec。分析器保留其他未解析的寄存器间接调用，不凭附近符号猜目标。

下表大小为ELF函数体字节数，不含独立literal池。11项大小在off/on完全一致：

| 函数 | A-off入口 | A-on入口 | 字节 |
| --- | --- | --- | ---: |
| send_message | 0x42000d88 | 0x42000e5c | 36 |
| ub_backend_send | 0x420019a0 | 0x4200216c | 148 |
| usb_kernel_send_sample_view | 0x4200c690 | 0x4200cf48 | 44 |
| wl_kernel_send | 0x4200a154 | 0x4200aa0c | 120 |
| encode_frame | 0x42009dec | 0x4200a6a4 | 82 |
| usb_kernel_encode_sample_view | 0x4200bca8 | 0x4200c560 | 27 |
| sample_encode | 0x4200d128 | 0x4200d9e0 | 21 |
| wlc_scalar_shape_1_encode | 0x4200c90c | 0x4200d1c4 | 158 |
| shape_1 measure | 0x4200e658 | 0x4200ef24 | 56 |
| wlc_put32 | 0x4200e5b8 | 0x4200ee84 | 50 |
| wl_kernel_native_none_seal | 0x4200e368 | 0x4200ec34 | 68 |

归一化保留opcode、寄存器、立即数和分支相对目标，只抽象重定位后的调用地址、literal槽及其具名引用。相等不代表链接字节或运行状态相等。

成功分支按冻结源码和实际指令逐项标注：kind=Sample、四个presence均true、有效指针、足够容量、普通snapshot空闲、编码及seal成功。业务函数合计337条静态执行指令，其中`wlc_put32`按四次计数；14次业务调用包含main调用send，不含两次读钟。它们不包括窗口溢出／回填、IRQ或抢占执行，也不是硬件retired计数。

四个fixed32值只改变写出的字节；presence检查、20 B编码长度和24 B帧长不变。`wl_kernel_send`末尾的`MULUH 0x38e38e39`用于9个TX位置的取模索引，off/on相同，没有按Sample序号奇偶切换业务路径的分支。计时内完成编码、seal和逻辑入队；实际USB提交、通知与owner推进在该调用链之外。

## 窗口异常向量与读钟已经避开IROM取指

六个`_WindowOverflow/Underflow{4,8,12}`均位于`0x40374000`起的`.iram0.vectors`。例如Overflow8在`0x40374080`，Underflow8在`0x403740c0`，各30 B，直接执行栈读写后`RFWO／RFWU`返回，无flash函数调用或L32R取flash literal。六段实际指令在off/on逐字节相同。

main栈也是片内DRAM：off为`0x3fc93380`、on为`0x3fc93880`，均8192 B。调用链有9层同时存活的CALL8深度，足以让window机制值得测量，但初始WINDOWSTART、IRQ及上下文决定实际异常次数，不能从深度直接计算次数或成本。

原`sys_clock_cycle_get_32`在off的`0x403792e4`、on的`0x403797dc`，均8 B、三条指令：

```asm
entry a1, 32
rsr.ccount a2
retw.n
```

因此不应通过改读钟函数或把窗口向量再迁IRAM来处理这次Sample回退。窗口栈访问、总线等待、返回后的IROM行以及抢占仍可能影响时延；“向量在IRAM”并不使这些成本为零。

数据位置也有变化：tx从`0x3fc988b4`移到`0x3fc98fd4`，endpoint从`0x3fc96ca0`移到`0x3fc97290`。这些对象和main栈均在片内DRAM；指令驻留候选需要与抢占、SRAM／总线等待区分，不能把相同的归一化指令理解为相同执行时间。

## 行足迹：先纳入literal槽，再筛选跨上下文候选

L32R读取的是指令地址空间内的literal槽。即使槽的内容是片内DRAM指针，槽本身仍经过ICache；因此IBUS miss不一定是某条业务指令的取指。正常业务实际读取的四个槽如下，另有main读钟指针槽`0x42000120`：

| 用途 | off槽 | on槽 | 内容 |
| --- | --- | --- | --- |
| tx指针 | 0x420000f8 | 0x420000f8 | 各自的片内tx |
| endpoint指针 | 0x42000178 | 0x4200024c | 各自的片内endpoint |
| 编码回调 | 0x4200081c | 0x420008f0 | 各自的编码桥接入口 |
| TX取模常量 | 0x42000428 | 0x420004f4 | 0x38e38e39 |

按32 B行覆盖实际指令全部字节、合并重复行并加入这些槽，得到：

| 范围 | off代码行＋literal行 | on代码行＋literal行 |
| --- | ---: | ---: |
| 标注成功路径＋读钟括号 | 35＋5＝40 | 32＋5＝37 |
| 同一组函数完整体＋读钟括号，含其他kind／错误分支 | 37＋6＝43 | 34＋6＝40 |

正常路径约1280／1184 B。on减少的三条代码行来自编码桥接、measure和put32的边界覆盖改善，而不是函数变短。两份路径都远小于16 KiB；**跨调用前序活动比“编码链本身容量不足”更值得优先区分。**

### 候选桶只用于筛选，实际index与访问序列尚未测得

几何量给出`16384/(8×32)=64`组。这里使用常规低位index模型：`bucket=(VA/32)%64`，相隔2048 B归入同桶。已查的TRM说明了cache结构与操作，但没有给出index选位或替换算法；以下是条件模型，不是芯片实际tag快照。

owner推进和USB组按真实调用职责收集函数完整体及literal，未做动态频率加权；既包含冷分支，也未穷尽所有callee。不能把整个函数的所有行都假定为两次Sample间执行。合并行后，原A-on有四个值得追踪的桶：

| 桶 | Sample相关行 | 跨上下文候选行举例 | 收集集合中的不同tag行数 |
| --- | --- | --- | ---: |
| 43 | 编码桥接0x4200c560 | close_idle入口0x42001560；推进0x4200ad60／0x4200bd60；atomic helper 0x4200ed60；USB线程／ISR等 | 9 |
| 53 | encode_frame入口0x4200a6a0；put32后半0x4200eea0 | 推进0x4200aea0／0x4200bea0；USBD 0x420056a0；UDC／ISR等 | 9 |
| 58 | generated send入口0x4200cf40；measure后半0x4200ef40 | 推进0x4200af40／0x4200bf40；DWC2 RX 0x42008740；UDC／ISR等 | 9 |
| 18 | endpoint literal 0x42000240；kernel send 0x4200aa40；shape末段0x4200d240 | observer enqueue入口0x42001a40；wait 0x42002240；RX borrow／USB等 | 9 |

**优先看桶43的编码桥接与idle退出交互，再看53／58的叶函数及返回行；18同时包含literal与enqueue observer，也应一并保留。** 桥接只有一条代码行，且对应周期idle helper的新增候选，判别范围比整个codec更小。具体行的全部贡献者可在JSON的`api_candidates`中查到。

桶43还有一个可具体核对的常规推进来源：A-on的`wl_kernel_step`在`0x4200ad5f`执行2 B的`beqz.n`，指令跨到`0x4200ad60`这一行。它是generation匹配后、CALL_FREE时跳过终态处理的分支，不只存在于错误／慢路径。off对应指令位于`0x4200a4a7`，没有跨32 B行边界。on的`close_idle`在`0x4200157f`的3 B L32R也跨行；入口会涉及`0x42001560`这一行。跨界本身是机器码事实；这两条行是否与`0x4200c560`桥接行竞争，仍取决于实际index及驻留。原始位置与覆盖行另存`boundary-witness.json`。

模型也给出反证：off的generated send／measure落在桶52，收集集合可达10行；on并非所有桶都“更拥挤”。仅按超8行的数量不能解释on为何更慢。单独Sample路径在此模型下每桶最多3行，也没有显示必然的自身冲突。

## observer的作用点在调用之间，并非只有阶段末snapshot

原A-on保留了三类会反复执行的flash路径：

- idle／ISR跟踪：`sys_trace_idle`、`sys_trace_idle_exit`、`sys_trace_isr_enter`及`close_idle`。main在Sample未到期时走1 ms等待，这些hook有接触前序驻留的机会。
- USB IRQ包围计数：`observe_dwc2.h`通过编译器`-include`包装ISR，实际A-on在ISR入口`0x420088ca`调用`ub_cpu_usb_irq_enter`，出口`0x4200903b`调用exit。原driver主体被并入该函数；不应按头文件结构假定额外完整调用层。
- 提交observer：链接`--wrap=usbd_ep_enqueue`，A-on执行`0x42001a58`的183 B计数wrapper，再调用真实enqueue。它位于提交路径，不在Sample API括号内。

这些周期observer函数体加literal共18条不同flash行；阶段snapshot／导出另计，未塞进周期竞争集合。运行时统计还会改变IRAM中的调度／IRQ路径、栈和时序，不能把全部observer影响约化为这18条行。

冻结main表明，每轮先做`ub_backend_service`，其中adapter service → kernel step → adapter service；随后才检查Sample是否到期。Sample成功后下一轮再推进提交。main未发送时还会经ready检查等待。因此前一帧提交、完成中断、USBD／UDC线程及idle退出，均处在下一次Sample的前序上下文中。相同USB IRQ次数不保证它们在读钟前后处于相同位置。

## 下一判别选择与本轮交付

下一轮若做动态判别，首选**保持原A ELF不变的PC／驻留观测**，从`0x4200c560`桥接入口、put32／measure及上述五条literal行开始，关联前序idle、owner和USB活动。先确认实际index或tag访问方式；若只有总miss数或会改变main布局的探针，就不能据此给这些桶定罪。当前没有理由对生成函数做重排、padding或IRAM迁移。

本轮交付可复算的静态筛选，不启动该动态实验。分析器及用法在`benchmarks/notification_progress/a_residency/`；4项工具测试和实际A-off/on分析通过。未构建任何新固件／host，没有新增需要保留的编译缓存或镜像；下载的手册缓存和中间分析目录已删除，只保留相关原文节选、源码和最终文本证据。
