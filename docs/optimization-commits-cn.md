# 优化迭代的 Git 提交索引

本文保留前一轮 A/B/C 的整理记录；最新的可靠 RPC／遥测共存批次见文末。

整理日期：2026-09-09。范围是多 RPC 维护体验、生成 API 内存收敛、帧与 RPC/codec
性能优化，以及 A 的取舍、B/C 生成器重构和后续性能复核。
Wirelink 与 WLC 是两个独立仓库，均在 `dev/wirelink-p0-hardening`。
本次只创建本地提交，**未推送、合并 main、创建 tag 或更新产品依赖 pin**。

## 提交边界

本轮累积改动此前尚未提交。本次按可审阅的最终功能边界整理，不伪造每次实验的历史。
WLC 的 A 阶段有完整冻结源码，故保留“功能/优化 → 生成快照 → B/C 重构”三个
可独立检出的边界；没有人为重建未经验证的 ABI 27/28 中间版本。
全程通过暂存区组织快照，没有回退或覆盖当前工作区。

### WLC：3 笔

起点：`c6b6a8fa560a15c45d564aad0afd197b13682de8`。

| 提交 | 内容与审阅重点 |
| --- | --- |
| `f8687c6` | `feat: adopt ABI 29 endpoint composition and optimized codecs`。组合 profile、send 路由、共享 handler 上下文/解码暂存、流式规范指纹、私有已验证 value 转换及编译期 codec 策略，保留 A 最终回退路径。 |
| `7f33374` | `test: freeze ABI 29 generated artifacts before refactoring`。5 类输入、41 个生成 C/H 产物的快照与可选逐字节编译器对照。 |
| `115f481` | `refactor: separate WLC planning and checked template emission`。B/C 职责拆分、共享编译计划与容量校验、非递归模板渲染；生成文件及 ABI 29 不变。 |

最终配套 WLC：`115f48132a5a761bc47f5c7880de940ea6fb2275`。
首次提交导出的 A 源码通过 129 项行为测试；快照提交也在该导出上通过冻结 CLI 对照。
重构后的最终源码通过 139 项测试，避免只验证最终树、却留下不可用的中间源码边界。
这些集成测试使用当前 Wirelink 核心，而不是任意历史版本。

### Wirelink：6 笔

起点：`337fede`。按提交顺序阅读：

| 提交 | 内容与审阅重点 |
| --- | --- |
| `9e3b3ea` | `perf: encode frames once and retain DATA across retries`。单遍 COBS、容量/重叠回退、重传复用、帧测试和带核心覆盖率插桩的 fuzz oracle。 |
| `867f09b` | `perf: scan RPC response-cache deadlines only once`。合并 deadline cache 扫描及对应状态测试。 |
| `43e2003` | `feat: add opt-in executor mutex wait and hold diagnostics`。分开记录锁等待/持有时间；默认关闭，未替换为无锁结构。 |
| `ca43283` | `feat: integrate ABI 29 profiles with a multi-service example`。CMake 配对、冻结 fixture 更新、12 服务示例、安装包与新增 RPC 验收、端点及安装文档。 |
| `d6a3c51` | `test: add opt-in host and H7 performance regression harnesses`。主机/UDP/executor、帧、RPC 验证和 codec 矩阵，以及 Zephyr/H7 工作负载与报告检查。 |
| 本文件首次新增的提交 | `docs: record optimization evidence and paired commit ledger`。汇总各阶段性能/正确性报告，更新 README、CHANGELOG 和本索引。 |

文档提交自身的哈希可用 `git log --diff-filter=A --oneline -- docs/optimization-commits-cn.md`
查询，避免在文件内维护自引用哈希。

## 配对与发布边界

- Wirelink 的 `ca43283` 及之后需要 ABI 29 WLC；最终验收使用上面的 `115f481` 源码。
  配置时显式设置 `WIRELINK_WLC_EXECUTABLE`，不能假设已有发布工具满足要求。
- 自动分发仍锁定旧 ABI 26 源码。缺少 ABI 29 配对时明确报错，不将旧工具当作兼容版本。
  推送、更新 CI/compiler pin 与源码归档 SHA-256 是后续发布集成工作，本次未执行。
- 本轮没有修改 libflorid、Ragtime_Firmwares 或 Touchstone。板端基准不等于产品迁移验收。
- 代码生成 ABI 从原始基线的 26 演进至 29；B/C 自身不再升级 ABI。
  本轮不改变冻结的链路帧、业务 codec 线上格式或托管 RPC v2 格式。

## 整理提交时重新验证

| 检查 | 结果 |
| --- | --- |
| A 源码暂存区导出的完整 WLC 行为测试 | 129/129 |
| A 加入快照后，与冻结编译器逐字节比较 | 5 类输入、41 个 C/H 文件一致 |
| 最终 WLC 全套（含同一逐字节 oracle） | 139/139 |
| `cargo fmt --check`、Clippy 全 targets/features、`-D warnings` | 通过 |
| 六组 benchmark 报告/分析器测试 | 29/29；不执行计时负载 |
| `build/maintainer-api` Release 重建与串行 CTest | 20/20，含 UDP、C/C++、Python FFI 和新增服务验收 |
| 两仓库暂存内容的 `git diff --cached --check` | 通过 |

本次不重复性能采样或操作 H7。此前的 Sanitizer、Zephyr 矩阵及实板结果保留于阶段报告，
不冒充本次重新执行。整理日志位于本机 `build/commit-closeout.KGB1zc/`。

## 性能证据阅读顺序

1. [多 RPC 维护体验](maintainer-api-progress-cn.md)及[静态内存/基准建立](api-performance-progress-cn.md)。
2. [Executor 与 H7 测量](executor-h7-performance-cn.md)：为何保留当前锁模型。
3. [帧编码](framing-performance-cn.md)及[回退路径](framing-fallback-performance-cn.md)。
4. [RPC 验证一次、复用结果](rpc-validation-performance-cn.md)。
5. [编译期 codec 计划](codec-plan-performance-cn.md)、[游标/key 收敛](codec-convergence-performance-cn.md)、[A 最终取舍](codec-fallback-performance-cn.md)。
6. [B/C 实现与正确性](wlc-refactor-progress-cn.md)、[重构后的性能复核](wlc-refactor-performance-cn.md)。

最后一项确认受测主机程序与 H7 BIN 逐字节不变，WLC runtime-only 生成 CPU 有稳定收益；
主机跨进程计时仍有明显噪声，不能据此声称端点运行时加速或建立严格百分比门禁。
各报告中的“未提交”“仅正确性”等描述属于当时阶段；当前 Git 状态以本索引为准。

## 保留但不纳入 Git 的材料

- 预先存在的 `AGENTS.md` 原样保留，不新增到索引。
- `wlc/` 是独立仓库，不作为 Wirelink 的普通目录或 gitlink 提交。
- `build/` 的冻结编译器、候选二进制、原始 JSON/RTT、一次性实验脚本和测试日志留在本机，
  不删除也不加入 Git。仓库保留可复用的基准源码、严格报告检查器和实验结论。

因此复用功能基准无需本机采样，但精确复核历史数字需要对应原始材料；
本次没有把仅在本机存在的材料描述为已经随 Git 分发。

## 追加批次：可靠 RPC 与高频遥测共存

起点为 Wirelink `c206c1e`。本批次在 `dev/wirelink-p0-hardening` 创建四笔本地提交，
不改写前一轮历史，不推送、不合并 main、不创建 tag，不更新产品依赖 pin。
WLC 没有新增改动或提交，仍配对 `115f48132a5a761bc47f5c7880de940ea6fb2275`，
生成 ABI 29、公开 context/storage 尺寸和线上帧格式不变。

| 提交 | 内容与审阅重点 |
| --- | --- |
| `ee578b3` | `perf: decouple reliable ACK waits from DATA transmission`。拆开可靠事务与物理 DATA，保留 ACK/重试优先级及异步借用；复用既有 payload 存储，覆盖迟到 ACK、取消/回收、时钟回绕、重叠存储拒绝和等待提示。同步更新中英文 API/协议合同。 |
| `74fea04` | `test: cover telemetry freshness under RPC loss and backpressure`。共享主机/Zephyr 混合流量模型、42 条记录、持续数据年龄、RPC 进展断言、独立旧核心对照，以及主机/H7 完整报告校验器。 |
| `0355a82` | `test: add H7 mixed-traffic and retry CPU measurements`。独立 H7 app，DWT tick CPU 与缓存/重编码重传微基准；要求 LTO，并明确模拟时间与真实 CPU 的边界。 |
| 本节所在的文档提交 | `docs: record mixed-traffic and H7 acceptance`。纳入两阶段完整报告、资源及重编码代价，更新 README、CHANGELOG 和本索引。 |

这是对已验证最终改动按职责拆分的提交，不伪造实验发生顺序。第二笔包含后续 H7 app
复用的报告校验器和可选计时 hooks；普通主机/Zephyr 构建不启用这些 hooks。
文档提交可用 `git log --oneline --grep='docs: record mixed-traffic and H7 acceptance'`
查询，避免自引用哈希。

### 本次提交前重新验证

- protocol / poll_hint：2/2 配置、56/56 用例，零警告。
- 主机 Release 重建后 20/20 CTest，含 C/C++、Python bridge、服务扩展等。
- 混合流量 Release 与 Clang ASan/UBSan 重建后，各 3/3 CTest。
- 四份既有有效 H7 捕获通过完整性校验；新旧 BIN/ELF、WLC 和全部捕获摘要匹配。
- 各提交暂存区 `git diff --cached --check` 通过。

整理日志位于本机 `build/mixed-traffic-commit.g1ozmB/`。本次不重复性能采样、不操作
H7，也不把之前的 37 配置/250 用例、WLC 139 项或实板采集称为本次重新运行。

### 证据与保留边界

1. [实现与模拟验收](mixed-traffic-progress-cn.md)：持续年龄、更新间隔、饱和场景
   RPC 代价、存储和调度边界。
2. [H7 实测](mixed-traffic-h7-cn.md)：四份有效记录，720 次正确 RPC，23,040 次计时内
   核心操作；正常平均 CPU 基本持平，缓存重传的小幅固定开销与重编码成本均保留。

H7 运行的是板内受控通道，年龄数字仍为模拟协议时间；它不是实际 USB/Willow 链路验收。
板上仍为新版独立 mixed-traffic 测试固件，不是 Willow；本次未改变停核及调试断开状态。
没有迁移 libflorid、Ragtime_Firmwares 或 Touchstone。

既有 `AGENTS.md` 原样保留且不纳入 Git；独立 `wlc/` 不作为目录或 gitlink 提交。
`build/` 的原始采样、冻结产物、无效 RTT 记录和日志保留在本机，不删除、不提交。
阶段文档中的“尚未提交”描述属于采集当时，当前提交状态以本节为准。

## 追加批次：端侧角色与传输的静态内存裁剪

本批次保持两个仓库的 `dev/wirelink-p0-hardening`，不推送、不合并 main、不创建 tag。

| 仓库 / 提交 | 内容 |
| --- | --- |
| WLC `d1632f2` | `feat: trim endpoint storage by local role and envelope`：profile 布局声明、角色/封装裁剪、ABI 30、C/C++ 组合测试与生成快照。 |
| Wirelink `20dda01` | `build: adopt ABI 30 role-specific endpoint layouts`：配套工具检查、current fixture 和 12 RPC 示例端侧 profile。 |
| 本节所在的文档提交 | `docs: record endpoint layout acceptance and paired commits`：中英文配置说明、尺寸数据、验证与本索引。 |

详细结果见[端点布局裁剪](endpoint-layout-cn.md)：WLC 142 项、主机 20 项、
Zephyr 12 配置 / 39 用例及相关 Sanitizer 验证。整理提交前重新运行布局与生成快照
4 项测试，日志为 `build/endpoint-layout.wbBqMM/precommit-tests.log`；不把上一阶段
完整回归说成此次提交时全部重跑。Cortex-M7 数字是编译布局，不是实板 CPU 测量。
previous fixture、线上帧格式、业务 C 文件和产品依赖未改变。
`AGENTS.md`、独立 `wlc/` 和 `build/` 仍按上文边界保留，不纳入 Wirelink 提交。
