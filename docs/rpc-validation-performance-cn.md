# RPC 内部验证复用：实现与性能记录

日期：2026-09-09。接续 ABI 28 与帧编码回退收敛。本轮仅推进 WLC 生成内部与配套
测试/文档；**生成 ABI 升至 29，线上格式、RPC domain、普通 owned API 语义不变**。
没有发布、tag、push、main 合并或产品依赖迁移。

## 实现边界

服务端原路径是：解码验证 → 规范化编码到缓冲 → 扫描指纹 → 再验证并转换 owned 值。
现在是：解码验证 → 规范化字段直接进入指纹计算 → 私有转换 owned 值。

- 普通编码器和指纹 sink **共用一份字段遍历生成模板**，在生成时特化，不在公开编码器
  热路径增加 sink 回调或逐字节模式判断。标量复用现有大小端/varint 写法。
- codec 的私有 sink 接受调用者提供的初始 hash；RPC domain seed 仍由 runtime 管理。
  不是对原始收包哈希：已知字段排序、presence、未知字段丢弃、浮点位模式均沿用规范化编码。
- 删除 canonical-request 字节缓冲、逐服务容量配置及对应初始化诊断项。高级无界 repeated
  仍须配置解码 backing；不再需要额外的规范化编码 backing。
- 私有转换仅接收未被修改的成功解码结果。公开 `value_from_view` 仍完整检查，失败不改输出；
  普通业务仍获得自持值，没有借用生命周期或新分配器负担。
- owned 输出整体清零一次，保留未使用尾部和 padding 的确定性；移除嵌套重复清零、用于
  默认值的整块临时 view，以及转换时的重复 UTF-8 检查。字符串/bytes 只复制实际长度；
  packed 数组是固定长度，仍须复制全部元素。**未取消按 owned 最大容量进行的外层清零。**
- 嵌套编码的长度前缀仍需计算子消息规范化大小；私有 size 路径跳过重复 UTF-8 扫描，
  但保留溢出等检查。因此不是所有嵌套结构都只遍历一次。

实现入口：`wlc/src/codegen.rs`、`fingerprint.c.in`、`value_codegen.rs`、
`runtime_codegen.rs` 和 `managed_rpc_request.c.in`。私有函数不在应用头文件中声明，
codec/runtime 必须成对重新生成；不会把一个公开的“跳过验证”开关交给业务。

## 可重复的测试口径

新增 [共享 CPU workload](../benchmarks/rpc_validation/README.md)，完整矩阵 65 组：
Small 的 string/bytes 上限 32，Large 上限 512，Nested 为 Large 加 128 个 packed float32。
实际字段长度为 0/16/32/256/512（Small 不含后两组），分别测 view-decode、canonical、
公开 from-view、请求处理链、公开 owned-decode。下文 `b` 是**每个** string/bytes 字段
长度，不是总报文长度；Nested 另含约 512 B 数组数据。

基线保留 ABI 28 WLC、生成 C 与二进制，不用当前生成器重建旧实现。两边均使用预计算
RPC domain seed；指纹均在 owned 转换前完成。早期 v1 harness 重复计算了基线 domain
且顺序不同，已修正为 v2，**不使用 v1 探测数据作收益证据**。

主机：Linux x86-64、GCC 16.2.1、Release `-O3 -DNDEBUG`、无 LTO，Google Benchmark
1.9.5。用户确认空闲后，A/B/B/A 串行运行，65 组各 7 次、最短计时 0.05 s；计时期间不
编译或并行跑测试。CPU 与 wall time 分开保留。最终重建后，两份已测候选主机二进制
SHA-256 均未改变。

H7：STM32H723ZG / dm_mc02，550 MHz，Zephyr 4.4.99，SDK 1.0.1 / GCC 14.3.0，
`-O2`、无 LTO、I/D cache 开启。两份镜像 A/B/B/A，最后再烧候选确认板状态；共五份完整
采集、**1,625 个计时批次、52,000 次操作**。每组预热后测 5 批 × 32 次，批内屏蔽中断，
校验和 RTT 均在批次外。基线两次各组中位数最大差异 0.019%，候选三次小于 0.062%。
解析器拒绝缺行、重复、复位混入、版本或时钟频率不一致的采集。

## H7 CPU 结果

请求处理链包含解码、规范化指纹和 owned 转换，**不包含缓存查找、业务 handler、
收发驱动和通信 RTT**。下表为 A 轮每操作中位数，单位 µs；B 轮重复确认相同趋势。

| 请求 | 优化前 → 优化后 | CPU 时间减少 |
| --- | --- | --- |
| Small，b=0 | 2.944 → 2.106 | 28.5% |
| Small，b=16 | 4.383 → 2.864 | 34.7% |
| Small，b=32 | 5.222 → 3.450 | 33.9% |
| Large，b=32 | 5.761 → 3.984 | 30.8% |
| Large，b=512 | 31.745 → 22.128 | 30.3% |
| Nested，b=0 | 22.977 → 21.099 | 8.2% |
| Nested，b=32 | 25.538 → 22.513 | 11.8% |
| Nested，b=512 | 55.084 → 40.759 | 26.0% |

两轮请求链全矩阵减少 **8.2%–34.7%**；公开 owned-decode 减少 **1.9%–11.2%**。
单独 canonical 的 Nested/b=0 仅减少约 1.8%，说明 packed 数组逐元素转字节与 64 位
指纹仍有明显成本，不能把字符串路径收益推广到所有消息形状。

代价同样保留：Small 独立 view-decode 从 1.071/1.203/1.319 µs 变为
1.098/1.295/1.411 µs，即慢约 **2.5%–7.7%（0.027–0.092 µs）**，两轮可重复；
Nested view-decode 约 +0.1%–0.3%。本轮没有改解码算法，代码布局/编译结果可能参与差异，
但尚未隔离原因，不能归为噪声或声称所有子路径均无退化。

| 整个独立 CPU 测试镜像 | 基线 | 候选 |
| --- | --- | --- |
| FLASH | 48,712 B | 51,504 B（+2,792 B） |
| RAM | 29,952 B | 29,952 B |

测试夹具为公平比较仍同时保留输入与 canonical 数组，故这里 RAM 不反映真实端点节省。
新 sink 是空间换时间的一部分，不能声称二进制也变小。最长候选批次约 1.305 ms；
生产固件不应复制批量关中断的测量方法。这不是 Willow 总 CPU 占用率测试。

## 主机与真实 RPC 路径

内部请求处理链两轮减少 **30.8%–51.2%**。公开 owned-decode 的 Nested/b=32 在 A 轮
出现 +8.1%、B 轮为 -3.1%，不据单轮判定稳定退化；Nested/b=256 两轮均约 +1.4%–1.5%，
保留记录，不用内部链路收益掩盖公开子路径差异。

另以现有 `benchmarks/api` 的 **12 服务、4 槽、reliable 真实 loopback RPC** 冻结二进制
对照，各 5 组 × 7 次 × 两轮。单调用 0/32 B 的 CPU 时间减少约 **1.8%–5.4%**，
4 个 32 B 调用组成的批次减少约 **3.1%–3.2%**。512/1024 B 的第二轮差异仅
0.06%/0.11%，不宣称这些大包整条 RPC 路径有稳定收益。此 workload 的请求与本轮
字符串/Nested 微基准形状不同，不能期待同样的百分比。

同一个真实 API workload：端点 `sizeof` **25,840 → 24,624 B（少 1,216 B）**，
runtime arena **11,934 → 10,902 B（少 1,032 B）**。这些是主机布局，不冒充 H7 产品 RAM。
UDP 正确性 smoke 已跑，未做本轮正式两进程网络延迟对照。

## 正确性与回归

- WLC 全套 **123/123**；Rust fmt/clippy 通过，包含 C11/C++、薄 FFI、owned、runtime
  大小门限、共享暂存及普通同步/异步端点测试。
- 新私有路径契约覆盖字段重排、未知字段、NEW/REPLAY/CONFLICT、非法 UTF-8、截断、
  无界 repeated backing、NaN payload、负零、整数极值、默认值和 512 组确定性随机消息。
  独立 canonical 编码/hash oracle 对照；验证一次 UTF-8、owned 清零一次、公开转换失败
  连 padding 都不修改。补齐 unsigned 默认值的 `UINT32_C/UINT64_C` 常量表达式。
- 新契约、owned 和 shared-scratch 合计 **6/6** 在 ASan/UBSan 下通过。
- Release 主机 CTest **20/20**；Clang ASan/UBSan **17/17**（排除 Python bridge 和独立
  Release 扩展构建）。覆盖 UDP 故障注入、教程、生命周期及 clock/session 集成。
- 新 benchmark CTest **4/4**，含 65 组 smoke 和解析器 **6/6**；现有 API benchmark
  CTest **10/10**，含真实两进程 UDP smoke。smoke 时间不作性能证据。
- ABI 29 全套生成 fixture 已再生成；Zephyr unit 加 protocol/application-runtime/bulk
  integration：**34/34 配置、236/236 用例**，覆盖 unit_testing、native_sim、Cortex-M3
  和 RISC-V32 QEMU。不是全仓库全平台。

首轮 Zephyr 配置误带入 Ragtime 产品模块，触发无关 C++ compiler-feature 配置错误；
改用显式 CMSIS 模块列表隔离后通过，没有为此修改产品构建系统或降低测试要求。

## 证据与后续

原始文件在本机 `build/performance-deps/`，不随源码提交：

- `rpc-validation-v2-{before,after}-{a,b}.json`、`rpc-validation-v2-compare-{a,b}.log`。
- `rpc-validation-h7-{before,after}-{a,b}.log`、`rpc-validation-h7-after-c.log`，
  对照与重复性报告为 `rpc-validation-h7-compare-*`、`rpc-validation-h7-*-repeat*`。
- `rpc-api-{before,after}-{a,b}.json`；`rpc-validation-final-*` 保存最终回归及 provenance。
- `rpc-validation-jlink.log` 为烧录、校验和断开记录。

```text
冻结 ABI 28 WLC:  1a1fecdd3fefb4aefb6b7dbcdfa7993b3e3c1d63cac75f1d742c7e4f21ea19e5
Host 基线:       394b425c4858b5b99146df2f9a5ea458f6a44fbf11375f4f022834eab0fa7b6d
Host 候选:       b964dbd2fd3bc27b62df2385b13fa811ee4169dbede38b067035372c1d72b169
H7 基线 ELF:     a601951bf24a71b391108348b03e7615e5a07403078ad4eb4646a809a5a4ba9e
H7 候选 ELF:     a390a5017c71aa57d12a8df222ebeb557874ab50bd767eaf6fb674590dcf7441
```

探针及采集均已退出；**H7 当前留的是本轮 RPC CPU 基准，不是 Willow 固件**。
libflorid、Ragtime_Firmwares 的源码和 pin 未改变；ABI 29 也没有可下载的发布配对。
后续最有依据的细化方向是 packed sink 的固定宽元素成本、独立小消息 decode 的布局退化，
以及将真实业务 profile 迁移后测 Willow CPU；本轮不混入无锁队列或新的 wire hash 算法。
