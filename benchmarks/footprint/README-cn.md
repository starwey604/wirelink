# 静态 footprint 诊断

[English](README.md)。当前格式为 `wirelink-footprint-v2`。

工具分别编译所有 `src/*.c`，记录对象段和实际编译器版本／flags。默认附带源码／镜像
hash；普通尺寸比较使用 `--no-digests` 省略完整性摘要。
这一总和包括可选独立帧工具、container、stream；它不是默认产品最终链接尺寸，
不能因可选对象存在就认为 native 内核仍承担旧执行成本。

C 探针读取已提交生成的 demo node 实际布局，以及 port、stream、byte FIFO、
call handle 尺寸。三种示例装配将节点字节分到 port、双 RX 槽、stream、TX 缓冲和节点其余存储（含成员对齐填充）。native 直接采用 `sizeof(demo_node_t)`。流节点按生成布局的相同前缀、所选连接器的收发上限与各成员的 ABI 对齐要求计算；每个 RX 槽多留一个字节。串口示例另外计入 FIFO 对象和字节暂存。
外层结构 padding、驱动对象不计入这些分项。

探针的源码对象总和不含 demo codec 的代码。实际产品 text／rodata／RAM 和全程序
布局必须使用最终 ELF／map／config、编译链接命令验证。`--elf` 仅记录段和 ELF hash，
明确缺少原始构建身份，因此不能自动认定配对／给出回归门槛结论。实板重构结果另附
合格镜像与资源账本。具体构建／运行命令见英文页。

解析器保留 v1 历史报告，拒绝跨 schema、mode、编译器比较，以及 v2 缺失／负数预算。
类型和代表性 storage 的变化是需要审核的合同变化；段大小可选择增长门槛。
每次结果都应保留源码、工具、镜像身份。大小不能推出 wall time／CPU 收益。
