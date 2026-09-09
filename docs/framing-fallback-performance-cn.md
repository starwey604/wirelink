# 帧编码回退路径收敛

日期：2026-09-08。接续[上一轮帧编码优化](framing-performance-cn.md)。
本轮完成 COBS 重叠/紧容量及 native packet 路径的 H7 收敛；不改 API、ABI 28、
线上帧、重试规则或产品依赖。**主机正在执行其他任务，最终实现的主机性能验收暂缓。**

## 实现与边界

- COBS 编码、精确计数使用局部状态，每个 span 结束才回写私有状态，减少逐字节
  结构体读写。空编码 span 立即返回；读取当前输入字节后才写输出，保留原地编码顺序。
- 复用一次保守的重叠判断。紧容量或别名仍精确计数；容量不足仍在任何输出写入前失败。
  若 payload 仅落在最坏长度范围、实际输出之外，允许一次多余 staging，但 staging
  只写实际输出区域。未增加 `restrict`，未取消输入输出重叠支持。
- 写帧头复用已经验证的头长度。native/length16 先移动 payload，再复制固定 4 B
  基础头及可靠帧额外 12 B；保留 `memmove`，不引入自制 memcpy 或平台汇编。
  小型 raw helper 使用标准 C `inline` 提示，目标编译器仍自行决定是否内联。
- DATA 编码缓存、ACK 独立缓冲和每次提交的新 I/O token 沿用上一轮实现。
  不新增分配、锁、时钟读取、公共配置或第二份帧缓冲。

中途试过“4 B / 16 B 二选一整头复制”，M7 编译器将 raw helper 外提，实测 native
小包编码反而变慢；已撤掉该版本。最终采用共享基础头的固定长度复制并检查反汇编。
代码布局也会影响结果，不把全部收益都归因于某条 C 语句，不以填充/地址对齐碰运气。

## H7 对照口径

STM32H723ZG / dm_mc02，550 MHz，Zephyr 4.4.99，SDK 1.0.1 / GCC 14.3.0，
`-O2`，LTO 关闭，I/D cache 开启。沿用未修改的 `wirelink-framing-v1` workload。

这里区分三份镜像，避免混淆“优化前”：

| 名称 | 内容 | 冻结构建目录 |
| --- | --- | --- |
| 原始实现 | 尚无单遍快路径和 DATA 编码缓存 | `build/framing-h7-before` |
| 上一版／本轮基线 | 已有快路径/缓存，但存在回退退化 | `build/framing-h7-after` |
| 本轮最终实现 | 在上一版之上收敛回退路径 | `build/framing-h7-fallback` |

前两份 ELF 哈希与上一轮相同，未重新构建基线。正式对照是本轮基线两次启动与
最终实现两次启动，每次完整 168 组 × 5 批，共 3,360 样本。额外重新烧录原始实现
核对历史退化，并第三次烧录最终镜像：六份有效采集共 **5,040 样本、161,280 次
计时内操作**，均包含完整 begin/end/pass；不把中间候选或无效采集计入。

每组覆盖 COBS/native、NONE、零/非零/混合数据、32/120/512/2048 B 和七种操作。
每批 32 次，先预热，DWT 计时内临时屏蔽中断；校验和 RTT 输出在批次外。
本轮基线两次启动各组中位数最大差异 **1.229%**；最终镜像三次启动任意两次比较
最大差异 **0.933%**。主机任务不进入板端计时区间；仍要求 RTT 无丢行、无损坏。

## H7 结果

下表为正式 A 轮每操作五批中位数；payload 每 17 B 一个零，单位 **µs**。
百分比相对“上一版”，不是相对最初实现：

| 操作 | Payload | 上一版 → 本轮 | CPU 时间减少 |
| --- | --- | --- | --- |
| COBS 重叠编码 | 32 B | 2.474 → 1.935 | 21.8% |
| COBS 重叠编码 | 120 B | 6.041 → 4.380 | 27.5% |
| COBS 重叠编码 | 512 B | 21.909 → 15.258 | 30.4% |
| COBS 重叠编码 | 2048 B | 84.062 → 57.841 | 31.2% |
| COBS 精确容量编码 | 512 B | 19.026 → 12.389 | 34.9% |
| COBS 精确容量编码 | 2048 B | 73.583 → 47.383 | 35.6% |
| COBS 常规编码 | 512 B | 12.002 → 6.437 | 46.4% |
| COBS 无重试发送及完成 poll | 512 B | 14.001 → 8.364 | 40.3% |
| COBS 三次超时重传并回收 | 512 B | 16.288 → 10.538 | 35.3% |
| native 重叠编码 | 32 B | 0.496 → 0.413 | 16.8% |
| native 重叠编码 | 512 B | 1.453 → 1.371 | 5.6% |
| native 重叠编码 | 2048 B | 4.509 → 4.426 | 1.9% |

两轮正式矩阵相对上一版：COBS 重叠减少 **19.6%–32.3%**，精确容量减少
**25.0%–49.4%**，常规编码减少 **25.1%–49.4%**。native 重叠减少
**1.8%–20.4%**，无重试 copy send 减少 **1.7%–5.6%**。
所有 168 组的两轮配对中位数均低于上一版；最小差异约 0.66%，接近测量波动，
因此不宣称每个小成本路径都具有可推广的稳定收益。

与重新烧录的**原始实现**比较，最终 B 轮 COBS 重叠减少 12.2%–28.9%，精确容量
减少 31.0%–66.2%；native 重叠减少 1.2%–12.2%。此前这些负载上的可重复退化
已经收回。常规 COBS 编码累计减少 42.3%–66.2%，三次超时重传总工作减少
62.7%–89.8%，上一轮缓存收益仍在。

| 整个测试镜像占用 | 上一版 | 本轮 |
| --- | --- | --- |
| FLASH | 52,364 B | 52,396 B（+32 B） |
| RAM | 42,368 B | 42,368 B |
| 公共 link context | 896 B | 896 B |

最大 32 操作计时批次由 2.690 ms 降到 1.852 ms（向上取整）。这是无外围业务、
热缓存、屏蔽中断的核心 CPU 微基准，**不是通信 RTT、固件总 CPU 占用或中断延迟**。
`overlap` 包含准备输入的复制，`claim` 也包含填充复制；重试模式测四次提交的总工作。
不能把此处关中断的批次搬进 Willow。CRC/length16 的板端性能尚未测量。

## 正确性验证

- Zephyr 全 unit 加 protocol integration：26/26 配置、201/201 用例通过，平台为
  unit_testing、native_sim、qemu_cortex_m3；不是全仓库全平台测试。
- 新增 frame integration 复用 unit 源码，验证目标编译器/libc：native_sim、
  Cortex-M3 QEMU、RISC-V32 QEMU 共 3/3 配置、36/36 用例通过。
- 新用例覆盖精确/最坏输出尾部的保守别名判断、短一字节时整个 arena 不变、
  输出前后哨兵、奇数地址与不同对齐的重叠、两种头长度及 ACK、三种完整性配置。
- Release 主机 CTest 20/20；Clang ASan/UBSan 主机 CTest 17/17（排除 Python
  bridge 和独立 Release 扩展构建）；WLC 及生成 C/C++ 消费测试 122/122。
- Google Benchmark 1,134 组正确性 smoke 通过，报告解析器 9/9。
  不使用 smoke 时间判断性能；主机最终实现尚待空闲环境下重新做正式 A/B。
- Clang framing/fuzz CTest 10/10，含六个 fuzzer 各 5,000 次 smoke。
  最终帧 fuzzer 额外 300,000 次通过，覆盖三种 envelope、输出地址模 8 对齐、
  尾部别名、短容量不写及输出范围外不变；核心同时启用覆盖率反馈和 ASan/UBSan。

## 证据与待办

本机 `build/performance-deps/` 保存原始数据，不随源码提交：

- `framing-fallback-h7-baseline-{a3,b}.log` 与 `framing-fallback-h7-candidate-{c,c2}.log`：
  正式两轮配对；`framing-fallback-h7-comparison-{a,b}.log` 为比较结果。
- `framing-fallback-h7-original-check.log`：本轮重新烧录原始实现；
  `framing-fallback-h7-candidate-c3.log`：最终镜像第三次启动及最终板状态。
- `framing-fallback-h7-*-repeat.log`、`framing-fallback-h7-original-comparison.log`：
  重复性及原始实现核对；`framing-fallback-h7-capture-sha256.txt` 保存六份采集哈希。
- `framing-fallback-final-unit*`、`framing-fallback-frame-targets*`、
  `framing-fallback-final-{release,api-sanitize}-tests.log`、`framing-fallback-wlc-tests.log`：回归证据。
- `framing-fallback-final-smoke.log`、`framing-fallback-unaligned-fuzz-tests.log`、
  `framing-fallback-unaligned-fuzz-300k.log`：最终 framing smoke、sanitizer 与 fuzz。
- `framing-fallback-{refined,fallback}.json` 是中间候选的主机探测；用户确认主机有其他
  CPU 任务，不用它确认最终实现的收益或无退化。不能把所有差异都简单归因于噪声。
- `framing-fallback-h7-baseline-a.log` 不完整、`baseline-a2.log` 混入复位前 RTT 残留，
  均被严格解析拒绝；`candidate-{a,b}.log` 是已淘汰实现，未并入最终样本。
- `framing-fallback-validation-jlink.log`：烧录/校验/退出；
  `framing-fallback-final-sha256.txt`：最终镜像及源文件身份。

```text
H7 上一版 ELF: 3111471d29be4b1a21c66db6ea9f4222a29dd722fc1b62fc0a99eb78e0e64404
H7 本轮 ELF:   adbda6084cb7ff989f6a4d5e2cacbcea224dfedf35000aded803b77729cfa220
H7 本轮 HEX:   a767763549f3560a3d12f95399b018b656d8534c861a9625d323414cb803e808
Host 本轮:     cf3d553d051ccc4535260f28f4c4b6a2570c5a413bc34ac7ad8890023b693619
```

探针和采集均已退出；**H7 保留本轮独立 framing CPU 基准镜像，不是 Willow 固件**。
本轮没有提交、push、tag、main 合并，未修改 WLC/libflorid/Ragtime_Firmwares 源码或 pin。
下一步在主机空闲时补做最终实现的性能对照，然后再决定产品迁移和完整 Willow HIL；
RX/CRC 融合扫描、端侧裁剪及无锁队列不混入本轮。
