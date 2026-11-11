# 单字节 varint 可行性检查：合同一致，快路径52→19条指令

## 建议进入生成器实施，暂不接受为性能基线

沿[H2D接收链检查](static-endpoint-receive-inspection-20261102-cn.md)，本轮在**冻结原A生成源码的独立临时副本**中验证了一个候选 V：只在通用 `getv` 开头增加单字节成功返回，多字节算法原文不动。

结果支持继续实施：GCC／Clang各完成386092次reader差分和22383次Chunk解码差分，ASan／UBSan通过。按原A编译命令生成的Xtensa对象中，单字节路径从52条指令、一次移位helper调用，降为19条、零调用。代价是reader正文增加21 B；所查二字节成功和单字节截断路径各多一条指令。

下一步应将这8行逻辑移入WLC模板，先跑生成器合同／输出检查和完整产品链接，再做无新增探针的A/V配对测量。原required-fixed B继续独立保留，不叠加到V。

后续[正式实现及产品筛查](static-endpoint-varint-product-result-20261111-cn.md)已完成：H2D主CPU目标改善5.74%，但off stream与on Sample p99出现稳定保护失败，候选未合入；main仅保留正式合同测试。

## 只提前处理完整、规范的一个字节

原型在 `size_t start = *at, n = 0U;` 前增加：

```c
  if (*at < length) {
    uint8_t first = in[*at];
    if (first < 128U) {
      ++*at;
      *out = first;
      return WL_CODEC_OK;
    }
  }
```

先检查可读范围，再只读取一次首字节。`0..127`的编码天然规范，不需要64位移位或重新计算编码长度。首字节带continuation位时，进入原算法前不修改at／out；空输入与越界起始at也仍由原算法返回原状态。不增加NULL检查、位宽限制、平台属性或公开API前提。

该变化影响通用reader的所有调用者，不只Chunk；因此后续不能只测H2D，也不能把key reader整体替换成32位函数。

## 差分同时比较返回值、游标和失败状态

reader参考函数直接提取自冻结A；原型与它逐次比较status、最终at和64位输出。失败时还检查输出保持哨兵值。覆盖：

- 全256种单字节、全65536种双字节组合，起始offset为0／3；单字节可用长度为0／1。
- 64位各bit边界的前值／本值／后值、UINT64_MAX、10000个确定性随机整数，各规范编码的全部截断前缀。
- 第十字节全部256种取值、九字节前缀为全`0x80`／全`0xff`、尾随数据、20000个随机原始输入及起始offset。
- 非规范零、截断和第十字节溢出的独立golden；空输入、at等于末尾、at超过末尾及SIZE_MAX，错误路径不读取NULL输入或写NULL输出。
- 精确长度的heap缓冲区和末尾读取，由ASan检查越界；这些分配仅在host测试中。

两编译器执行相同语料，每份reader状态计数为：OK 151271、MALFORMED 230803、OVERFLOW 4018，合计386092。

Chunk差分另克隆原getv／skip／read_value／decode，和原型公开decode使用相同未改动的clear／lookup／fixed reader与描述符。逐次比较status及整个view输出的字节，包括失败时的presence、已解字段和未改padding。覆盖规范输入的全部截断、39个位置各256种字节变异、0／1／32／127／128／1024／1025 B长度与截断、乱序、重复、未知字段、过宽key、空输入及10000个随机输入，共22383例。

host使用严格C11、`-Os -Wall -Wextra -Wpedantic -Werror`和ASan／UBSan。此处验证的是冻结USB schema与通用reader；其他schema／生成模式的正式输出、完整WLC测试和lint留在模板实施阶段，不用本轮测试替代。

## Xtensa工作量减少，路径代价并非全部相同

使用原A-on保留的 `device/compile_commands.json`，只替换源文件／对象输出路径并优先包含冻结生成头文件。保留原SDK、配置、`-Os`、`-mlongcalls`、LTO off等编译条件；正式配置原用C17，host合同另按C11编译。未改编译策略来追数。

重新编译的原reader与已测原A-on的操作／寄存器／CFG及163 B正文相同；对象中的ROM helper relocation仍为`__ashldi3`。191个text／literal section逐字节比较，仅`.text.wlc_getv`正文不同：163→184 B，entry栈帧均48 B，literal section未增加。

下表均是标注路径的镜像内指令数，**不展开ROM helper体**。多字节行输入为`80`重复n−1次再接`01`，即该宽度最小规范值，不泛指该宽度所有数值。

| 路径／输入 | 原A | 原型V | ROM移位调用，A／V |
| --- | ---: | ---: | ---: |
| 单字节0..127 | 52 | 19 | 1／0 |
| 2字节最小值 | 93 | 94 | 2／2 |
| 3字节最小值 | 134 | 133 | 3／3 |
| 4字节最小值 | 175 | 172 | 4／4 |
| 5字节最小值 | 216 | 211 | 5／5 |
| 6字节最小值 | 255 | 248 | 6／6 |
| 7字节最小值 | 294 | 285 | 7／7 |
| 8字节最小值 | 333 | 322 | 8／8 |
| 9字节最小值 | 372 | 359 | 9／9 |
| 10字节最小值 | 412 | 397 | 10／10 |
| at等于末尾 | 16 | 16 | 0／0 |
| at超过末尾 | 17 | 17 | 0／0 |
| `80`，length=1，截断 | 49 | 50 | 1／1 |

算法原文不变，新增分支仍影响寄存器分配；所查二字节成功和截断路径因此各多一条指令。原型没有新增helper调用；快路径也不执行reader仍为多字节保留的ROM literal load。

规范32 B Chunk的两个key和一个length都在单字节域，按这三个reader调用可减少99条所列指令和三次ROM调用。完整新镜像还需检查链接后的路径、对齐／literal搬迁和资源；本轮对象反汇编使用统一显示基址`0x42000000`，它不是V的实际链接地址，不用于cache驻留推断。

## 下一步只实施这个候选，再决定是否上板接受

1. 在原生成器main基础上，将上面的逻辑移入 `wlc/src/codegen/templates/common.c.in`，固定差分参考与测试。保留wire、错误优先级、at／输出状态、所有现有调用入口和生成模式。
2. 运行受影响及完整WLC测试、lint、生成输出确定性和产品构建。检查实际linked fastpath、完整资源及其他接收路径；不把对象正文+21 B直接当成最终镜像增量。
3. 通过后使用原gated host作共同主机端，对无新计数的产品A/V按既有smoke／boundary、同状态配对与保护线筛查。完整跟踪RPC、低频、Sample和全部stream分量，不以一个方向的改善覆盖另一个方向的损失。

原A/B回退的最终cache／总线／调度归因仍未解决；本候选的采用由新的产品实测决定。

## 可复用交付与清理

- 工具与测试：`benchmarks/notification_progress/a_residency/varint_fastpath.py`、`varint_contract.c`、`test_varint.py`；复算命令见同目录README。
- 证据：`/home/ww/codings/wirelink-varint-feasibility-20261102/`，保留reader补丁、原型／参考对象的文本反汇编与relocation、编译命令、完整标注路径及差分计数。`result.json`为结果，`checks.json`记录验证入口。
- 三项新工具测试通过。全部临时生成副本、host可执行文件和Xtensa对象随检查退出永久删除，没有固件／ELF／对象归档、Rust缓存或新镜像；正式WLC未修改。
- 本轮未访问串口／USB、未刷写或做恢复smoke。上次交付的最后健康固件仍记录为 `owner-local-20261102-B-on` 诊断版，身份见上一轮 `handoff.json`；它不作为后续产品A。
