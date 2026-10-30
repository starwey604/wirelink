# 静态端点内核重构：实施记录

日期：2026-10-30。对应 [计划](static-endpoint-kernel-refactor-plan-20261030-cn.md) 和 [K0 合同](static-endpoint-kernel-contract-20261030-cn.md)。当前为 **K0 完成，K1 共享内核与真实生成端点通过软件验证，USB 产品集成尚未完成**，不是 K2 性能验收结论，也不是 K6 最终结果。以下先保留首批记录，再登记第二批进展。

## 已完成并提交的点

| 提交 | 完成的工作 |
| --- | --- |
| `a5f6800` | 独立正确性修复：TX commit 使用活动 claim 保存的容量，不能相信调用者修改后的 span。进入旧 A，不计作重构收益。 |
| `876df6f` | K0：登记计划、新合同、所有权和容量；冻结实际 dirty 输入、平台及生成器，独立构建新旧 A 基线产物和共享旧 host。 |
| `3e29c90` | K1 基础：独立 packet 端口，2 槽 RX、单物理 TX、非回绕 token、claim/abort/borrow/release、consumed/drained、通知、stop/quiescence。 |
| `d00c57a` | K1 基础：实际 native/NONE 连接与原托管 RPC v4 元数据原语，不调用通用 frame encoder，不引入旧执行引擎。 |
| `25bdf60` | 端口 reinit 正确性：先快照配置，允许停止并确认静默后传入端口自身保存的配置；增加回归用例。 |

新代码集中在 `experimental/static_kernel/`，是独立 CMake 工程，没有给默认产品添加新旧模式开关。公共消息 codec、WLC 源码、默认安装目标和现有 USB adapter 尚未迁移。原有 8 份已修改文档逐字节保持冻结时内容，WLC 独立仓库保持干净；未修改既有研究目录。

端口序号和 completion 标志在同一个 lock-free atomic word 中比较更新，避免旧 publisher 在检查 token 后恰好遇到新 submit 时污染新传输。客户端逻辑终态尚未实现，不能把端口 completion 测试解释成 timeout/cancel 所有权已通过。

## 验证及证据

证据目录：`../wirelink-static-kernel-20261030/`。`baseline/input.json`、`baseline/platform.json`、`old-builds.json` 和 `hosts/o*/runner.json` 包含旧输入与产物身份。`k1-primitives/source.tar.gz`、`k1-primitives/validation.json`、各编译器的 `CMakeCache.txt`、测试日志、测试可执行文件和 archive 封存本批新代码及验证。

| 检查 | 结果 |
| --- | --- |
| GCC 16.2.1，strict C11，`-Wall -Wextra -Wpedantic -Werror` | 3/3 host tests 通过 |
| Clang 23.1.4，同上并启用 ASan/UBSan | 3/3 host tests 通过 |
| owner/I/O 跨线程 RX/TX 及旧 completion 竞争 | 100,000 次传输通过 |
| Ztest `type: unit`，`unit_testing` | 6/6 cases 通过 |
| RPC 元数据与冻结旧 A helper 对照 | 1,000,000 组合法、变异和随机输入通过 |
| native/NONE 与冻结旧 A framing tool 对照 | payload 0–2048 B 共 2049 个长度通过 |
| Xtensa GCC，`-Os`，native/NONE object 的 `nm -u` | 无未定义符号；无 CRC、COBS 或通用 frame 调用依赖 |

测试覆盖容量篡改、两槽满／释放、同一地址的旧 claim、RX cursor 的 u32 回绕、ID 耗尽、BUSY/FAILED、返回前 completion、先 consumed 后 drained、旧实例／旧 transfer、stop 后完成和 reinit。wire 测试覆盖 operation 全部编码宽度、正负拒绝状态及 INT32 边界、截断、非最短／溢出元数据、零身份、拒绝尾随数据、普通帧容量和错误帧后下一条正常帧。

首次 Ztest 构建失败是测试 target 的 strict C11/pedantic 选项作用到了 Zephyr 自身使用 GNU 扩展的宏。选项改为仅作用于便携核心源文件后通过；最初失败日志保留在 `twister-packet-port/`，最终报告在 `twister-primitives-final/`，没有覆盖失败证据。

复现主要命令：

```sh
cmake -S experimental/static_kernel -B build/static-kernel-gcc \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build/static-kernel-gcc --parallel 4
ctest --test-dir build/static-kernel-gcc --output-on-failure

cmake -S experimental/static_kernel -B build/static-kernel-asan \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_FLAGS='-Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/static-kernel-asan --parallel 4
ctest --test-dir build/static-kernel-asan --output-on-failure
```

在初始化的 Zephyr workspace 中运行：

```sh
west twister -T /home/ww/codings/wirelink/tests/zephyr/unit/static_packet_port \
  -p unit_testing --outdir <新的证据目录>
```

## 首批结束时尚未完成

K1 下一点是紧凑调用／TX 存储状态及真正的 WLC 生成合同：4 个 call、4 个待发回复、typed handler、七消息容量和直接路由。随后接 USB 产品 adapter，再验证 4 槽乱序、双向 RPC、副作用前响应 admission、BUSY 不重执行业务、timeout/cancel/close 唯一终态等端到端合同。

这些通过前不进入 K2；本批没有打开 USB/串口、刷写固件或采集性能。旧 A 的新构建不等于新 A 的实板测量，目前没有本轮 RTT、MCU active、Sample 或吞吐收益结论。默认产品删除和仓内消费者迁移留待 K2 的继续规则及后续 K3–K5。

## 第二批：共享状态与 WLC 生成连接

Wirelink 提交 `7409077` 完成紧凑共享内核：唯一四槽调用表、四请求 frame、四回复 frame 和单普通消息 frame 描述符。配置中的 spans 不再重复存入内核；逻辑 terminal callback 与物理 TX 租约分别管理。timeout 包含排队时间，cancel 不内联 callback，generation 快照排除 callback 新建的调用；递归 step/close 报重入错误。回复 admission 在 handler 之前完成，BUSY 只重提已编码 frame，满池保留 RX 借用。没有 replay、ARQ 或服务端业务延期状态。

WLC **独立仓库提交 `ac6c29b`** 增加 `compile-kernel`、profile v2 验证、typed handler/client/direct/send 路由、实际 codec 上界、部署与产物 manifest。复用现有私有 managed tagged codec 和公共 Schema-v1 emitter，不调用旧 runtime generator。profile v1 旧默认保持原状；新入口要求显式 v2，旧 reliable、guarantee/replay、mapped、deferred、LATEST/FIFO 声明失败。K1 仅支持 native/NONE、both 角色及有限静态编码上界，其他部署显式失败。临时保留原生成入口仅为原型隔离，K5 前仍需统一产品。

本批七消息 fixture 的 `schema/usb.wl` 与冻结 A 的业务 schema 字节相同。真实生成代码通过产品 packet API 和共享内核运行，覆盖：四调用乱序、双向 RPC、请求快照、重复／晚到响应、四回复池满时副作用前背压、BUSY 单次执行、逻辑成功早于本地 TX drain、业务拒绝、请求／响应 decode 错误、响应 encode 失败释放，以及 Sample、1024 B StreamChunk、Progress 和 512 B ControlResponse。fixture handler 是软件测试业务，不能代替 benchmark 的真实 identity、START/STOP 和观测导出集成。

第二批验证：

| 检查 | 结果 |
| --- | --- |
| GCC strict C11，含真实生成代码 | 5/5 host tests 通过 |
| Clang strict C11 + ASan/UBSan，含真实生成代码 | 5/5 host tests 通过 |
| Ztest unit_testing，共享端口／wire／调用内核 | 10/10 cases 通过，`twister-kernel/` |
| 完整 `cargo test` | 197 tests 通过，44 个测试执行单元，`wlc-kernel-all-tests-r2.log` |
| clippy all-targets/all-features，`-D warnings` | 通过，`wlc-kernel-clippy.log` |
| 非 benchmark schema：有符号整数、带 NUL 的 bounded string、bytes、空消息 | 生成 C 以 GCC `-Os` 及 Clang ASan/UBSan 编译运行通过 |
| 移除语义诊断、组合确定性、CLI 无旧 runtime 产物 | WLC 4 项专项测试通过，`wlc-kernel-sanitized.log` |
| Xtensa GCC strict C11，`-Os -fno-lto` | core、packet port、wire、生成端点编译通过；无旧引擎依赖，见 `kernel-xtensa/` |

严格编译修正了共享 encoder 的局部 measured length 初始化；算法及合法 wire 不变。原始完整 Rust 检查因该源码多出 5 个字符而触发五份 provenance 快照差异；检查仅相关 `.c` 的 size/digest 变化后更新快照，完整重跑通过。Clang 还指出直接放进 `.c` 的完整私有 inline codec 存在未使用入口；改为生成私有 codec 头文件复用，没有关闭 warning、增加属性或改动 codec 算法。首次 Rust 失败日志保留在 `wlc-kernel-all-tests.log`，Clang 失败诊断保留在本轮工具输出；不能作为已通过的检查。

Xtensa 对象测得 `sizeof(wl_kernel_t)=416 B`、生成 endpoint **4400 B**（已含 core、typed response scratch 和三个 TX 池）、packet port **88 B**；RX 外置 **4096 B**，当前这三者合计 **8584 B**。请求池 4×53 B、回复池 4×544 B、普通池 1036 B。旧 A 配置是四 client slots、四 server pending slots、四 server cache slots及 540 B cache 响应容量；新 B 是至少四个独立已编码待发回复，不保留历史。这里尚未计入新 USB adapter、全部线程栈、driver/observer 或 heap，不能据此宣布整个集成 RAM 达标或性能改善。

下一点是把 USB 产品 adapter 从 `wl_ctx_t` 改接 packet port，并将 benchmark 的真实 Setpoint／Control／Sample 业务接到生成 API。随后补齐完整 K1 生命周期与产品 I/O fixture，再登记 K2 的共同旧 host、A→B→A→B→A 筛查。本批仍未访问 USB/串口或刷写固件，没有本轮性能结果。
