# 原 A 的 cache tag 映射与调试通道核对

> 本轮研究已收敛。下文实验命令及分支状态为历史记录，临时入口已删除；当前状态见[收敛记录](static-endpoint-cpu-convergence-20261111-cn.md)。

## 本轮推进了映射依据，运行时驻留采集仍缺独立调试连接

本轮继续沿原A、不增加逐调用探针的方向，完成两项判别：

- **上一轮的64组、2048 B同组周期得到官方S3 ROM实现的支持。** off/on共83条候选行重新分类后全部保持原分组；编码桥接、idle退出、空调用槽扫描的组43候选仍值得优先检查。
- **ROM自带tag遍历不是透明的实时读取。** 两种遍历函数均暂停cache，第二种还写tag-query选择寄存器。保持ELF不变不等于保持执行环境不变，不能把这种遍历插入原业务后继续当作未扰动的Sample计时。

主机当前只枚举到COM的USB-UART桥、运行基准的USB OTG及其他主机设备，没有独立JTAG适配器。用户确认板上的USB和COM已都接好；这两根线满足当前刷写、日志和基准使用，不提供同时运行USB业务的独立JTAG通道。因此本轮没有取得原A的运行时PC／tag快照，也没有新的耗时归因或产品优化结果。

## 现有两个端口为何不能同时提供这次调试通道

COM对应QinHeng USB-UART桥，当前序列号`5CF9120624`；基准USB序列号`10003B6DEC94`，以`2fe3:574c`运行。主机有OpenOCD程序，但没有发现可用的外部适配器或正在运行的debug server。此调查只读取USB/sysfs和工具信息，未打开串口、复位或attach目标。

ESP32-S3的USB-OTG与USB-Serial/JTAG共用内部PHY；要同时使用这两个USB控制器，需要另一个PHY，或采用独立的pad JTAG路由。见[官方USB Device说明](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/peripherals/usb_device.html)。本轮不切换现有PHY，不修改eFuse，不用破坏基准连接的“切到USB-JTAG”代替同工况观测。

动态采集的恢复条件是：有独立JTAG连接，并确认目标有无需改写eFuse的可用路由。用户提供适配器／接线后，由agent验证连接和恢复路径，再采集PC或受控驻留快照。当前建议先保留现有两根线和A，不为这一步改板或购买外部PHY。

## 选择匹配芯片的官方 ROM，而不是相似名称的其他芯片

此前有效flash记录识别芯片为ESP32-S3 revision0.2。使用[Espressif ROM ELF发布20260528](https://github.com/espressif/esp-rom-elfs/releases/tag/20260528)中的`esp32s3_rev0_rom.elf`；该发布只提供S3 rev0，按[官方修订号选择规则](https://github.com/espressif/esp-rom-elfs#choosing-the-rom-elf-file)选择不高于芯片修订号的版本。发布中同时存在S31文件，未将其作为S3证据。

下载按发布的checksum验证传输完整性。提取后核对公开入口与本地SDK linker声明：

| ROM公开入口 | 地址 | 实际实现 |
| --- | --- | --- |
| Cache_Travel_Tag_Memory | 0x400019d4 | 0x4004f93c |
| Cache_Travel_Tag_Memory2 | 0x400019e0 | 0x4004fb68 |
| Cache_Get_Virtual_Addr | 0x400019ec | 0x4004fd90 |

公开入口通过L32R／JX跳到对应实现。该证据来自官方发布及SDK软件契约，未从板上读出ROM字节作逐字节比对。

## ROM如何给出64组与2048 B周期

`cache_mode`中，size位于offset0，line_size位于offset4，ways位于offset6，icache位于offset7。A的配置为16384 B／8路／32 B。

`Cache_Get_Mode`的实际指令会读取ICache配置并写入4或8路，不能照抄SDK结构注释里的“always 4”。`Cache_Get_Virtual_Addr`在`0x4004fd93..0x4004fdb3`的计算为：

```text
way_span = cache_size / cache_ways = 16384 / 8 = 2048 B
VA = 0x42000000 + ((tag × way_span) & 0x01ffffff) + vaddr_offset
```

同模式下，legacy tag遍历的filter路径计算`filter_addr % way_span`，再按32 B切分组；每路64个tag槽、每槽4 B。由此得到：

```text
set   = ((VA - 0x42000000) % 2048) / 32     // 整除，0..63
tag   =  (VA - 0x42000000) / 2048          // 整除，14 bit
slot  = 0x600c6000 + 4 × (way × 64 + set)  // 16 KiB、8路、一个bank
```

上式用于本轮这一种配置，不推广到32 KiB或4路bank布局。地址重建helper的实际反汇编被一个只支持其整数／load／branch指令的离线解释器执行：覆盖64组、每行offset0／31、四个地址tag，共512例；计算与反向定位一致。没有在目标芯片上执行这个helper。

## 编码桥接、idle与推进仍落在组43

| A-on候选行 | set | 地址tag |
| --- | ---: | ---: |
| 编码桥接0x4200c560 | 43 | 0x18 |
| 空调用槽分支涉及行0x4200ad60 | 43 | 0x15 |
| close_idle入口行0x42001560 | 43 | 0x02 |
| adapter service行0x4200bd60 | 43 | 0x17 |
| atomic helper行0x4200ed60 | 43 | 0x1d |

组43的legacy tag槽推导为`0x600c60ac、0x600c61ac、…、0x600c67ac`。这是槽地址计算，不是已读取的内容。上一轮off的43条、on的40条候选行全部与ROM映射匹配。

映射核对没有补上动态访问顺序：原集合仍包含未执行的函数分支，off的某组仍可收集到10行，不能仅凭on组43有9行就认定冲突或计算miss成本。实际替换算法也未被本轮验证。

## 两种tag入口都不是零扰动读取

### legacy aperture

`Cache_Travel_Tag_Memory`在ICache路径调用`Cache_Suspend_ICache`，之后构造tag memory地址并调用用户callback，结束时Resume。`Cache_Suspend_ICache`会修改cache控制／总线屏蔽状态、等待硬件状态并处理autoload／freeze接口，不只是读取计数器。

### tag-query寄存器

`Cache_Travel_Tag_Memory2`也会先Suspend。ICache分支选择object=1，写查询VA，逐way读取tag content：

| 地址 | SDK字段 | 本轮确认的作用 |
| --- | --- | --- |
| 0x600c4180 | CACHE_TAG_OBJECT_CTRL | 选择ICache／DCache对象 |
| 0x600c4184 | CACHE_TAG_WAY_OBJECT | 选择way0..7 |
| 0x600c4188 | CACHE_VADDR | 查询地址 |
| 0x600c418c | CACHE_TAG_CONTENT | 读取／写入选中tag内容 |

这些作用可在ROM `0x4004fc21..0x4004fd24`及本地SDK `extmem_reg.h`交叉核对。CONTENT本身为R/W；本轮没有向任何寄存器读写，也没有生成可直接运行的live读写命令。

SDK的ICache tag字段为valid bit0、lock bit1、attributes bits2–5、fifo字段bits6–8、地址tag bits9–22。离线`decode_tag`保留这些字段；valid为0时不输出驻留行。一个way无效不代表这个地址在其他way也不在，更不是已测得的miss。字段名称fifo也不足以确定整个替换策略。

受控停机快照将来可用于判断停止时的驻留，但停机／Resume会改变后续调度和USB时序，不能将其后的elapsed混入原始A时延系列。应把PC／驻留证据与未停机的业务计时分开评价。

## 工具、证据和清理

新增`benchmarks/notification_progress/a_residency/tag_mapping.py`及对应测试，复用上一轮反汇编解析与原A inventory，不改原分析入口的参数和数据含义。

证据位于`/home/ww/codings/wirelink-cache-tag-20261101/`：连接调查、ROM发布元数据／checksum、相关函数和公开入口反汇编、SDK节选、`mapping.json`及检查记录。四项新工具测试、512例ROM helper对照和83条旧候选行重分类通过。

下载的ROM ELF及压缩包已永久删除，不留编译payload归档；原A作为当前基线仍保留。本轮未刷写、未复位、未连接调试器，也未构建新固件。删除ROM后，可仅用文本证据复算：

```sh
/home/ww/zephyrproject/.venv/bin/python -B \
  benchmarks/notification_progress/a_residency/tag_mapping.py \
  --inventory /home/ww/codings/wirelink-a-residency-20261101/final/result.json \
  --rom-asm /home/ww/codings/wirelink-cache-tag-20261101/Cache_Get_Virtual_Addr.asm \
  --out /absolute/new-mapping.json
```
