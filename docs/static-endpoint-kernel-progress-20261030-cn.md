# 静态端点内核重构：首批实施记录

日期：2026-10-30。对应 [计划](static-endpoint-kernel-refactor-plan-20261030-cn.md) 和 [K0 合同](static-endpoint-kernel-contract-20261030-cn.md)。当前为 **K0 完成、K1 基础组件完成，端到端原型尚未完成**，不是 K2 性能验收结论，也不是 K6 最终结果。

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

## 尚未完成

K1 下一点是紧凑调用／TX 存储状态及真正的 WLC 生成合同：4 个 call、4 个待发回复、typed handler、七消息容量和直接路由。随后接 USB 产品 adapter，再验证 4 槽乱序、双向 RPC、副作用前响应 admission、BUSY 不重执行业务、timeout/cancel/close 唯一终态等端到端合同。

这些通过前不进入 K2；本批没有打开 USB/串口、刷写固件或采集性能。旧 A 的新构建不等于新 A 的实板测量，目前没有本轮 RTT、MCU active、Sample 或吞吐收益结论。默认产品删除和仓内消费者迁移留待 K2 的继续规则及后续 K3–K5。
