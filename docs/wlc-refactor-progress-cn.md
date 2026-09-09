# WLC 可维护性重构：B / C

日期：2026-09-09。接续 [A 的性能取舍](codec-fallback-performance-cn.md)。
本文件记录 B/C 的实现与正确性验收阶段；用户随后授权的计时另见
[重构后性能复核](wlc-refactor-performance-cn.md)，不混入本阶段的测试结论。
本轮不改变 A 保留的 codec 策略，不迁移产品 pin，不提交/合并/tag。
按用户要求只验证正确性，**不运行性能 benchmark 或 H7 性能采集**；构建并行度为 2。

## 范围与验收顺序

1. 冻结 A 编译器；建立紧凑生成快照，并与冻结 CLI 逐字节比较全部生成文件。
2. B：按职责拆分 codec/runtime 生成器，C 模板从 Rust 大字符串中移出。
   此阶段生成文件必须不变；通过完整 WLC 测试后进入 C。
3. C：集中 codec 编译期决策与常量，收敛模板渲染；替换针对整个 C 程序的名称替换。
   不增加消息展开、平台策略或运行时分支；再次检查生成文件不变。
4. 最终运行 WLC、C/C++/FFI、UDP 功能、Sanitizer 及 Zephyr 合同回归。
   跳过 benchmark 计时/计时 smoke；不以构建或测试耗时给出性能结论。

仓库此前只记录 B 的分层/模板方向，未保存 C 的细分原文；上面的 C 是本轮明确的实施范围，
已向用户提出非阻塞核对。更大范围的前端重写或引入模板框架不包含在内。

## 当前进度

- 冻结编译器：`build/wlc-refactor-before/wlc`，SHA-256
  `8fa4f345a3f5d9c5e00624d62b65d078403e755ae7fce1fd0ea6df2e840ea671`。
- 基线快照覆盖 5 类输入、41 个 C/H 文件：全类型/查找阈值/固定数组、空消息发送、
  retained/send、managed RPC、无界显式 RPC，包含独立 codec/runtime 命名空间。
- B 已完成：codec 按命名、容量、声明、描述符、引擎和绑定拆分；
  runtime 按校验、声明、存储组装、retained、RPC、dispatch、pump 拆分。
  B 阶段的 129 项 WLC 测试通过，生成文件与冻结 CLI 逐字节一致。
- C 已完成：`CModel` 统一生成前校验和容量计算；`MessagePlan` 集中查找策略、
  规范字段 key 和单固定数组配方。runtime-only 不再生成并丢弃 codec，
  存储组装和默认端点复用同一份消息容量结果。
- 编码/指纹共用带显式 sink 参数的模板；RPC/endpoint 先生成完整子片段，再插入父模板。
  `template::render` 只扫描原模板，插入值不递归展开，缺失/重复参数在生成阶段报错。
  不引入外部模板框架，不改变 A 的阈值、清零、游标或回退路径。
- 最终 Release 编译器 SHA-256：
  `5357df82922234b5c187dd06bbdd96fda912321301880533248e2a6faf46a017`。

## 正确性验收

| 检查 | 结果 |
| --- | --- |
| WLC 全套（包含新规划/模板/校验一致性测试） | 139/139 通过 |
| cargo fmt / Clippy 全 target/feature，警告视为错误 | 通过 |
| 冻结编译器对照 | 5 类输入、41 个 C/H 文件逐字节相同，golden 未更新 |
| 当前/上一版 conformance fixture 重新生成 | C/H 与 manifest 均与现有文件一致 |
| 生成 C 的 ASan/UBSan | codec_plan / owned_values / rpc_validation / shared_scratch，11/11 通过 |
| 主机 Release CTest | 20/20 通过，含 C/C++、Python FFI、UDP 故障场景和新增服务验收 |
| 主机 Clang ASan/UBSan CTest | 17/17 通过；Python/extension 由上面的 Release 覆盖 |
| Zephyr 合同矩阵 | 34/34 配置、236/236 用例通过，无警告；2 个静态过滤配置不计入通过数 |

主机使用 `build/maintainer-api` 与 `build/maintainer-api-sanitize` 重建，
显式使用本轮 Release WLC。Sanitizer CTest 排除 `python|extension`，未使用未插桩的
Python 进程加载 ASan 共享库来充当检查。所有 benchmark 选项/计时测试保持关闭。

Zephyr 选择 `tests/zephyr/unit` 和 integration 下的 `protocol`、
`application_runtime`、`bulk_transfer`；平台为 `unit_testing`、`native_sim`、
`qemu_cortex_m3`、`qemu_riscv32`，使用 `-j2`。从已有 Ragtime Zephyr 工作区调用
`.venv/bin/west twister`，SDK 为 `/home/ww/zephyr-sdk-1.0.1`，
`ZEPHYR_MODULES` 仅指定该工作区的 `modules/hal/cmsis_6`，避免带入产品配置。
输出为 `build/performance-deps/wlc-refactor-zephyr/`；没有运行板端程序。

本轮 B/C 实现和上述正确性验收均已完成。未提交/推送、合并 main 或创建 tag；
已有 A 的性能取舍及采样产物保持不动。

## 维护者阅读顺序

先看 [WLC 源码导航](../wlc/docs/codegen.md)，再按问题选择文件：

1. `codegen.rs` / `runtime_codegen.rs`：对外入口及产物边界。
2. `codegen/plan.rs`、`bounds.rs`、`wire.rs`：哪些事实可由编译器决定。
3. `codegen/engine.rs` 与 `templates/`：生成 C 的算法及两个输出 sink。
4. `runtime_codegen/assembly.rs`、`endpoint_codegen.rs`：如何使用容量结果完成默认组装。
5. `tests/codegen_snapshot.rs` 与 `tests/codec_plan.rs`：产物不变和运行行为正确的两道验收。

这是责任边界收敛，不是单靠换目录宣称总代码量减少；较长的运行时胶水仍可在有具体修改需求时
继续按职责提取。本轮没有新增类型展开、ABI 升级、产品迁移或性能结论。

## 快照边界

`wlc/tests/codegen_snapshot.rs` 保存紧凑 manifest（长度及已有诊断摘要），避免提交巨大的
生成 C 金样。普通 CI 检查快照；本轮另外设置 `WLC_REFERENCE_COMPILER`，进行真正的逐字节
比较，不把摘要相同当作精确字节比较。快照不能替代 C 消费、畸形输入与所有权测试。

本机过程日志保存在 `build/performance-deps/wlc-refactor-*`。
