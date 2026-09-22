# 静态足迹基准

[English](README.md)。

这个基准测量 Wirelink 在构建期的开销，而不是运行期：每个核心翻译单元的代码
与常量数据大小，以及库为若干代表性配置要求调用方预留的静态 RAM。它只需要一个
C 编译器和支持 `-A` 的 GNU/LLVM `size`。

Wirelink 不做任何分配、也没有隐藏状态，所以“足迹”是两件事：链接器产出的可加载
代码/只读数据，以及该配置要求的调用方存储。报告两者都记录。

## 运行

objects 模式用接近目标的 flag（`-Os -ffunction-sections -fdata-sections
-fno-asynchronous-unwind-tables`）逐个编译核心源文件，测量其 `.text`、
`.rodata`、`.data`、`.bss`：

```sh
python benchmarks/footprint/footprint.py --json footprint.json
```

要匹配某个具体构建，传入相同的编译器和额外 flag。取值以 `-` 开头的 flag 需要
用 `=`：

```sh
python benchmarks/footprint/footprint.py --cc gcc --cflags=-O2 \
  --json footprint-o2.json
```

固件镜像直接从链接好的 ELF 测量，这是准确数字，因为它包含 dead-code 消除和跨
模块内联：

```sh
python benchmarks/footprint/footprint.py --elf build/rx/zephyr/zephyr.elf \
  --size-tool ~/zephyr-sdk/gnu/xtensa-espressif_esp32s3_zephyr-elf/bin/xtensa-espressif_esp32s3_zephyr-elf-size \
  --json footprint-esp32s3.json
```

`size` 必须支持 `-A`（System V 输出）。本工具不构建固件；先用 `west` 构建好
镜像。

## 对比

```sh
python benchmarks/footprint/compare.py before.json after.json
python benchmarks/footprint/compare.py before.json after.json \
  --max-growth-percent 5
```

对比会按分组给出字节增量：区段总量、每个模块或 ELF 区段、存储契约、公开结构
大小。新出现或消失的模块报告为 `new`。增长阈值是专用 runner 的可选门禁，不是
默认值：编译器和 C 库更新本来就会让代码大小浮动几个百分点，所以始终用相同的
编译器、flag 和模式对比。`--elf` 与 `objects` 报告不能互相比较。

## 报告内容

| 字段 | 含义 |
| --- | --- |
| `totals` | 区段分类总量：`text`、`rodata`、`data`、`bss`，另有 `debug`（调试/展开/注释，不加载）与 `other` |
| `modules` | objects 模式下每个源文件的大小 |
| `sections` | ELF 模式下每个区段的大小 |
| `storage` | COBS `NONE`/`CRC32C` 在 20/120/2048 字节 payload 下，以及一个非 COBS 配置的 `wl_config_requirements()` 输出 |
| `types` | 公开结构大小（`wl_ctx_t`、`wl_event_t`、`wl_config_t`、`wl_storage_t`、`wl_storage_requirements_t`） |

存储字段是调用方的契约：该配置需要的 `tx_payload`、`tx_unit`、`control`、
`rx_fifo`、`rx_fallback` 字节数。它们是编译期常量，所以这里的变化就是每个使用
方 RAM 预算的变化，也是本基准最需要跟踪的东西。

## 不测什么

- objects 模式是链接前的。没有跨模块内联、没有 `--gc-sections`、没有 LTO，
  所以总量比任何链接后的镜像都大。链接后的数字用 `--elf`。
- object 大小取决于编译器、版本、flag，以及 `.text.*` 这类区段命名。只能比较
  使用了同一工具链字符串的运行，报告会记录该字符串。
- 探针报告的是配置**要求**的存储，不是某个程序实际用的；产品预留更多不会体现。
- host object 大小是趋势与按模块归因的信号，不是固件镜像。目标板上的 flash 和
  RAM 请测目标 ELF。
- ELF 模式会统计它能归类的所有区段；分配器、bootloader 与厂商区段不在本工具
  关注范围内。

## 流程

改动库之前先冻结一份基线报告，记录编译器版本与 flag；改完后重建 candidate 再
对比。保持模式、编译器、flag 一致；只有工具链变化时才刷新基线。RX 路径改动时
[docs/rx-performance.md](../../docs/rx-performance.md) 要求给出 image 与 RAM
大小，本基准就是产生该数字的方式。
