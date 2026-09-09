# 第一方向：帧编码与重试复用

日期：2026-09-08。范围是 Wirelink 核心的编码 CPU 工作，不改 RPC API、不换锁、
不迁移产品依赖。实现、主机测量和 H7 两轮 A/B 对照已完成；常规路径收益成立，
H7 的重叠输入和部分精确容量路径仍有可重复退化，保留为下一步收敛项。

这是首轮结果的历史记录。后续已完成 [H7 回退路径收敛](framing-fallback-performance-cn.md)，
包含新一轮配对测量与当前板状态；下面的旧镜像数据保持原样。

## 本轮实现

1. **常规 COBS 单遍编码**：输出容量覆盖最坏情况、payload 与输出不重叠时，省去
   逐字节精确长度预扫描。保留紧容量和重叠输入的精确计数与 staging；容量不足仍不写输出。
   回退计数先累计固定字节数，再只统计额外的非零块码字节，避免每字节重复累计。
2. **编码后的 DATA 复用**：当前 stop-and-wait 只保留一笔 DATA，已有 `tx_unit`
   可以直接保留编码结果。BUSY、ACK 超时和异步 I/O 失败重试不再重新封包、计算 CRC 或 COBS。
   每次向 sink 提交仍生成新的 I/O token，ACK 定时起点、重试预算和取消规则不变。
3. **不提前回收传输资源**：ACK 继续用独立 control buffer；取消后若 I/O 还在途，
   仍须等完成再取走结果。新发送始终使旧编码失效，失败、完成和回收路径清理缓存标记。

没有新增 heap、锁、时钟读取或第二份帧缓冲。私有 context 增加一个长度字段，
仍容纳在既有 896 B opaque storage 中；公共布局容量、代码生成 ABI 28、线上字节均不变。
本轮没有删除 retained payload，也没有宣称实现了端到端 zero-copy。

## 基准口径

新增 [`benchmarks/framing`](../benchmarks/framing/README.md)，只依赖核心与可选
Google Benchmark。与 [H7 app](../benchmarks/zephyr/framing/README-cn.md) 共用 C 负载。

- 主机：Linux x86-64、GCC 16.2.1、Release `-O3 -DNDEBUG`、无 LTO、Google Benchmark 1.9.5。
- 正式矩阵：7 种模式 × COBS/native × NONE/CRC32C × 3 种数据分布 × 4 种长度，
  共 336 组，每组 5 次，最短采样 0.02 s，随机交错执行。
- 反向长采样复查：54 组 COBS/NONE，每组 7 次、0.05 s；另复查 native/NONE 和
  32 B 重叠用例。测量期间不并行运行本轮构建/测试，但机器仍启用动态频率与 ASLR。
- 计时包含操作内的返回值检查、常数时间的 sink 计数；初始化、预热和独立 oracle 校验在外。
  `retry3` 包含初发、三次重传及 cancel/take；`busy3` 包含三次 BUSY 和最终 SENT。
  二者都是四次提交的总 CPU 工作，**不是 RTT，也不表示实际丢包率**。

因此以下为指定负载的主机 CPU 成本，不能直接换算 H7 整机占用率或网络延迟改善。

## 结果

长采样复查，COBS/NONE，混合 payload（每 17 B 一个零），每操作 CPU 时间中位数：

| 操作 | Payload | 优化前 → 后 | CPU 时间减少 |
| --- | --- | --- | --- |
| 常规帧编码 | 120 B | 75.50 → 41.73 ns | 44.7% |
| 常规帧编码 | 512 B | 307.89 → 161.86 ns | 47.4% |
| 常规帧编码 | 2048 B | 1180.94 → 601.16 ns | 49.1% |
| 无重试发送及完成 poll | 120 B | 88.18 → 54.73 ns | 37.9% |
| 无重试发送及完成 poll | 512 B | 325.16 → 176.33 ns | 45.8% |
| 三次 BUSY 后完成 | 120 B | 353.46 → 79.06 ns | 77.6% |
| 三次 BUSY 后完成 | 512 B | 1300.58 → 199.11 ns | 84.7% |
| 三次超时重传并回收 | 120 B | 385.86 → 85.78 ns | 77.8% |
| 三次超时重传并回收 | 512 B | 1311.37 → 205.47 ns | 84.3% |

正式完整矩阵中，COBS/NONE 常规编码减少 32.8%–48.5%，copy send 减少
23.9%–45.7%；CRC32C 常规编码减少 9.7%–14.8%，因为本轮没有优化 CRC 的扫描。
CRC32C 的重复提交同样可跳过重新算 CRC，COBS 三次重试总工作减少约 70%–78%。

### 退化、修正与取舍

- 第一版快路径让部分紧容量/重叠路径变慢；长采样确认混合 512 B 紧容量约慢 15.5%，
  不是直接忽略为噪声。调整回退计数后，最终 COBS/NONE 长采样 54 组中最差变化为
  全零 512 B 精确容量 +0.51%；混合 512 B 重叠为 316.92 → 309.52 ns。
- 正式全矩阵仍有小成本回退：32 B 重叠部分组约多 2–5 ns，native/NONE 无重试
  send/claim 多约 1–3 ns，部分百分比接近 10%。这些路径本来不需要 COBS/CRC 扫描，
  缓存维护和新增分支不是免费的。额外反向长采样中，混合数据 native send 只多
  0.23–0.33 ns（约 0.8%–1.9%），32/120/512 B claim 多 0.29–0.81 ns，2048 B
  claim 略快；32 B 重叠多 0.09–1.49 ns。首轮最差百分比并不稳定，不把 10% 当作
  固定开销，也不把总体收益描述成“所有链路都更快”。
- native/NONE 三次 BUSY/超时的总工作仍降低约 32%–46%；CRC32C 重试的收益更大。
  当前产品使用 COBS/NONE，因此保留这项内部优化，不为几纳秒增加公开配置或业务分支。
  若未来主要负载改为 native/NONE，应继续针对这一路径重新评估。
- 主机微基准函数布局和分支预测会影响结果；不据此推断 H7 的具体收益，也不启用共享 CI
  硬性性能门槛。比较脚本支持专用 runner 自选 CPU 退化阈值。

## 正确性验证

帧测试用独立 standalone COBS 编码器作为 oracle，覆盖 DATA/可靠 DATA/ACK、三种
integrity、零/非零/混合数据、空 payload、254 字节块边界到 2048 B、精确容量、
输入输出重叠、空输出指针、短一字节且输出不变。

协议测试通过链接器 wrap 统计实际 `wl_frame_encode` 次数，不给生产热路径加计数器：
三种 envelope × 三种 integrity × copy/claim；五次 sink 提交只编码一次 DATA，
中途 ACK 独立编码、不破坏 DATA，过期 I/O token 不生效。补充取消后的在途保护、
首次 I/O 失败和超时后新消息不复用旧帧。既有时钟回绕、生命周期和 frozen-v1 测试继续覆盖。

Zephyr 全部 unit 加 protocol integration 在 unit_testing、native_sim 和 qemu_cortex_m3
执行：26/26 配置、199/199 用例通过，另一个不适用配置被静态过滤；不是全仓库全部平台覆盖。

主机 Release 回归 20/20，Clang ASan/UBSan 回归 17/17；后者排除 Python bridge
及独立 Release 扩展构建，不把这两项计作 sanitizer 覆盖。WLC 及生成 C/C++ 消费测试
122/122。新基准完整 1,134 组 smoke 通过；报告解析器 9 项通过。
Clang framing/fuzz 构建 CTest 10/10，包括六个 fuzzer 的 5,000 次 smoke。
额外帧编码 fuzz 100,000 次通过，核心本身也启用了覆盖率反馈与 ASan/UBSan，
不是仅对 harness 分支做反馈。随机输入还覆盖 payload 位于输出前后和输出内部的别名位置。

## H7 实测

独立镜像使用 dm_mc02/STM32H723ZG、Zephyr 4.4.99、SDK 1.0.1 / GCC 14.3.0，
实际 `-O2`、LTO 关闭、I/D cache 开启。默认 ISR table 配置不满足 LTO 依赖，
已把 app 配置改为显式关闭，避免“请求了 LTO 就算启用”的误判。

| 构建占用 | Before | After |
| --- | --- | --- |
| FLASH（整个测试镜像） | 52,108 B | 52,364 B |
| RAM（整个测试镜像） | 42,368 B | 42,368 B |
| 公共 link context | 896 B | 896 B |

使用冻结的两份镜像按 before-A、after-A、before-B、after-B 顺序重新烧录、复位和采集。
每次 Flash program/verify 成功，RTT 都取得完整 begin、840 样本与 end/pass，
共 **4/4 完整矩阵、3,360 样本、107,520 次计时内操作通过**，不含预热。
每份镜像覆盖 168 组：七种操作、COBS/native、三种数据分布、32/120/512/2048 B、NONE。
报告频率为 550 MHz；DWT 每批测 32 次操作，初始化、预热、校验及 RTT 输出不在计时内。

同一镜像 A/B 两次启动，各组 CPU 中位数最大差异：before **0.746%**，after **0.713%**。
下面引用 A 轮每操作五批中位数，单位为 **µs**；不要与前面的主机 ns 混淆。
Payload 为每 17 B 一个零的混合数据，COBS/NONE：

| 操作 | Payload | 优化前 → 后 | CPU 时间减少 |
| --- | --- | --- | --- |
| 常规帧编码 | 120 B | 4.700 → 3.273 µs | 30.4% |
| 常规帧编码 | 512 B | 17.961 → 12.002 µs | 33.2% |
| 无重试发送及完成 poll | 512 B | 19.929 → 14.003 µs | 29.7% |
| 三次 BUSY 后完成 | 512 B | 76.219 → 15.813 µs | 79.3% |
| 三次超时重传并回收 | 512 B | 78.206 → 16.288 µs | 79.2% |
| 三次超时重传并回收 | 2048 B | 287.269 → 51.813 µs | 82.0% |

两轮完整矩阵中，COBS/NONE 常规编码减少 **22.0%–33.9%**，copy send 减少
**10.7%–32.3%**，三次 BUSY 总工作减少 **55.1%–82.0%**，三次超时重传减少
**58.6%–82.0%**。不把四次提交的总工作称为单次发送成本或链路 RTT。

### 板端退化与剩余工作

- COBS 重叠输入慢 **1.9%–12.3%**；例如 120 B 为 5.618 → 6.041 µs，
  32 B 为 2.203 → 2.475 µs。部分精确容量路径也慢，混合 512 B 为
  17.964 → 19.025 µs（+5.9%）。常规端点的分离、足量 TX storage 不走这些回退路径。
- native/NONE 无重试 copy send 慢 **0.9%–3.5%**，claim 慢 **0.8%–3.0%**；
  native 重叠输入个别组慢约 **13.1%**。native 三次 BUSY/超时总工作仍减少约 **42%–55%**。
- 这些变化可在两次重新烧录后重复，不能因主机复测接近持平就忽略。这里没有单独隔离
  编译器生成代码、函数布局和缓存维护的贡献，不把差异全归因于一个分支或推广到所有 ARM。
  **下一步先收敛 M7 回退编码的 CPU 成本，保住常规路径收益，再评估产品迁移。**

这是热缓存、屏蔽中断的纯核心 CPU 微基准，不是 Willow 电机/CAN/USB 整机负载，
也没有验证真实通信 RTT 或 CRC 模式的板端性能。32 次一批的最大计时区间：before
**9.193 ms**，after **2.690 ms**；该实验特意关闭外围业务，**不能把这样的关中断批次
带进产品运行循环，也不能据此声称中断响应改善**。

### 探针与最终板状态

本轮按住 RESET 时 DAP 可以初始化，但 CPU halt 一直超时；松开后，同一 Commander
使用 `exec SetAllowStopMode = 0`、SWD 1000 kHz 成功接入。随后四次切换镜像均无需再按键。
复位后、`g` 之前出现 RTT control block 尚未初始化的提示；启动后自动恢复，四份数据无缺行。

测试结束已关闭采集和 Commander。**板上保留 after 独立 framing CPU 基准镜像，
不是 Willow main，也不再是上一轮的 1 kHz CPU HIL。**

## 证据与后续

原始数据保存在本机 `build/performance-deps/`，不随源码提交：

- `framing-final-{before,refined}.json`：最终 336 组正式矩阵。
- `framing-confirm-{before,refined}.json`：COBS/NONE 54 组长采样复查。
- `framing-tradeoff-{before,refined}.json`：native/NONE 与小包重叠的额外复查。
- `framing-final-comparison.log`、`framing-confirm-comparison.log`：中位数对比。
- `framing-tradeoff-comparison.log`：额外复查的小成本路径，不掩盖首轮波动。
- `framing-h7-{before,after}-rtt-{a,b}.log`：四次 H7 完整原始采集。
- `framing-h7-comparison-{a,b}.log`：两轮 before/after 对比；
  `framing-h7-{before,after}-repeat.log`：同镜像重复启动的一致性检查。
- `framing-h7-validation-jlink.log`：连接尝试、四次 program/verify 和退出记录。
- `framing-h7-capture-sha256.txt`：四份原始 RTT 与两份已烧录 HEX 的 SHA-256。
- `framing-before.json`、`framing-after.json`、`framing-*-2.json`、`framing-*-3.json`、
  `framing-refined-probe*.json`：首版和中间候选，不与最终数据混用。
- `framing-final-*-tests.log`、`framing-final-wlc.log`、`framing-fuzz-instrumented-tests.log`、
  `framing-encode-fuzz-instrumented-100k.log`：回归与 fuzz 日志。
- `framing-final-unit.log` / `framing-final-unit-rerun.log`：新测试最初误写 BUSY 和
  TX_TIMEOUT 的预期值，按既有合同校正断言后通过，未为此修改生产返回值或事件。
- `framing-verified-unit.log` / `framing-verified-unit/twister.json`：最后重新完整执行的
  26 配置、199 用例，全部通过。
- `framing-final-sha256.txt`、各构建目录 `CMakeCache.txt` / `.config` / 编译命令：构建身份。

测量二进制 SHA-256：

```text
Host before: 12725b361894deca4a2187867ccf1e8d9b81f2d4db0e32b9659f7e3cf60dac37
Host after:  97894dd9e1b9e6cb9fa815e8c28aa9cfe6e41f0ea8182ad858974e9a2a460953
H7 before:   cae132cb533a422ed1ed08d058feded5806a68b764b26a6d0dea898055fe1609
H7 after:    3111471d29be4b1a21c66db6ea9f4222a29dd722fc1b62fc0a99eb78e0e64404
```

Wirelink 基点 `337fede0a9d10f9ca63a9658784ff8707dc7c170` 加开发工作区修改；基线是
本轮改动前的实际工作区，不是回退到旧发布版。冻结目录为 `build/framing-before`
和 `build/framing-h7-before`，候选为 `build/framing-refined` 和 `build/framing-h7-after`。
WLC/libflorid/Ragtime_Firmwares 本轮未改源码或依赖 pin；没有提交、push、tag 或 main 合并。

H7 同负载 A/B 已完成；后续[回退收敛记录](framing-fallback-performance-cn.md)已处理
上述板端退化。最终实现的空闲主机性能对照及产品 HIL 仍待推进。
CRC 融合扫描、RX 解码、端侧角色裁剪和无锁队列不混入这一轮。
