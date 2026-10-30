# Wirelink 兼容性策略

> 英文版 [`compatibility.md`](compatibility.md) 是规范来源。

本开发树为 Wirelink `0.12.0`、生成契约 `0.12`。执行重构有意破坏 0.8 的 C/C++
API、ABI、adapter 和生成接口，没有兼容包装；请用匹配 WLC 一起重编译消费者。
本轮保留 protocol-v1 与 codec 字节，见[执行合同](execution-contract-cn.md)。
已发布 0.8 是独立源码配对。兼容性分为多个独立
演进的域。

## 线上协议

Protocol v1 由 compact prefix 的版本位 `01` 标识。字段、字节序、envelope、
integrity trailer、ACK 行为和 decoder 拒绝规则以 [`protocol-cn.md`](protocol-cn.md)
为规范，并由 [`conformance-v1.md`](conformance-v1.md) 冻结。

声称兼容 v1 的实现必须重现 conformance fixture 中每个精确 transmission unit。
改变输出字节或让既有合法包失效，需要新协议版本和迁移策略。新 packet/flag 含义只能
使用保留值，v1 peer 仍会拒绝不支持的标准含义；NACK 保留。被淘汰的 22-byte 草案从未
对外承诺兼容。v1 不协商 link profile，双方通过带外方式约定 envelope、integrity、
MTU 和 payload limit。

## C 源码与二进制接口

安装面由 `include/wirelink/` 的 public header，以及 `Wirelink::wirelink`、
`Wirelink::loopback`、`Wirelink::diagnostics` CMake target 组成。后两者独立链接；
`Wirelink::host` 只在构建可选 C++20 runtime 时安装。`rx_ring_state.h` 和 `src/`
中的文件即使源码 checkout 可见也属于 private。
可选 `Wirelink::platform` 提供默认时钟/身份环境，不给核心增加 OS 依赖。

`wl_ctx_t` 是通过 `max_align_t` 对齐、可静态分配的 opaque 类型。
`WL_CONTEXT_STORAGE_SIZE` 及其对齐为 v1 预留；应用不得检查或持久化 private bytes。
public enum-like domain 通常使用固定宽度 `int32_t` typedef 与命名常量，避免 native
enum 宽度和 `-fshort-enums` 影响。含 pointer/`size_t` 的结构仍依赖目标架构。

1.0 前，`0.x` minor 之间仍可修正 source/ABI，并记录在 `CHANGELOG.md`；CMake 只把
同一 `0.12.x` minor 视为 package-compatible。1.0 起遵循语义化版本：不兼容 public
C API/ABI 需要新 major；minor 可以新增函数和 enum value，应用的 switch 应保留
`default`。

生命周期和并发规则也是 API：一个 producer 拥有 RX feed/reserve/DMA，一个 consumer
拥有 poll、event release、adapter service 和 reset；event payload 借用到
`wl_event_release()`；sink TX 指针有效到同步完成或匹配的 `wl_tx_consumed()`。

## C++ / Python bindings

可选的 `Wirelink::cpp`、`Wirelink::cpp_host` 和 WLC 生成的 SDK 提供 v0.12 源码 API，须与匹配的 C 库一起重编译，不承诺跨编译器的稳定 C++ ABI。Binding source API revision 2、WLC 生成契约 0.12 和 Python 扩展 ABI 相互独立。

Revision 2 提供同步 RPC、C++ 异步操作和 Python `AsyncClient`。wheel 私有链接原生代码，不跨独立 SDK 交换原生句柄。安装方式、支持范围及迁移要求见 [v0.8.0 发布说明](release-v0.8.0-cn.md)。

## WLC Payload Schema

协议把 payload 当作由 `message_id` 分发的不透明字节。WLC schema version 独立于
link header version。兼容演进规则见 [`schema-v1-cn.md`](schema-v1-cn.md)：数字 ID
永久保留，删除后继续 reserved，field wire identity 不变，未知字段可跳过，允许新增
optional 字段。

固定数量 packed numeric 字段的元素类型、数组长度和 packed cardinality 都属于 wire
identity。`string<MAX>`/`bytes<MAX>` 的 bound 也属于 wire identity；增删或修改 bound
都需要新的 field/message identity。生成前必须让 WLC 对照旧 schema 检查兼容性。
不兼容 payload 通常应分配新 message ID，而不是悄悄重定义旧 ID。

## 平台 Adapter

C core 不拥有硬件、OS handle、thread 或 scheduler。adapter 可以按平台节奏演进，
但必须保持 core buffer ownership 和 completion 规则。Zephyr adapter 跟随 Wirelink C
版本；Astrial adapter 当前是源码集成的 C++20 组件，不属于 installed core ABI。

实时性能是平台和配置相关数据，不是兼容性承诺。benchmark 结果必须同时记录 board、
toolchain、clock、baud rate、payload、ingress mode 和 percentile 方法。

开发合同 0.10 还替换了 0.9 的 RPC 客户端 encoded response 池：结果改为每调用
owned value，删除重复解码 API。客户端响应必须有静态上界；服务器 replay 字节不变。

P3 / 合同 0.11 加入按方法声明的 RPC 保证。省略或显式 `replay` 保留 canonical 重复抑制及有限响应历史；`correlate` 允许每次合法到达独立执行，仅保留 pending/响应 TX 容量，不能用于替代同保证性能门槛。当前应配套重建 core、WLC 和 SDK 为合同 0.12；1.0 前的生成契约必须精确匹配。

P4 / 合同 0.12 支持显式 `positional_v1` 业务体及普通 route 的独立 codec。默认 tagged/replay 不变。codec/runtime 必须配套生成，source/header profile 错配会被拒绝。全数值域、严格拒绝、identity 及演进边界见 [positional-v1](positional-v1.md)，消费者迁移见 [0.12 指南](migration-0.12-cn.md)。
