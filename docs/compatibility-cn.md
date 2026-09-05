# Wirelink 兼容性策略

> 英文版 [`compatibility.md`](compatibility.md) 是规范来源。

Wirelink `0.9.x` 是 protocol v1 和目标 1.x C API 的候选发布线。兼容性分为多个独立
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

`wl_ctx_t` 是通过 `max_align_t` 对齐、可静态分配的 opaque 类型。
`WL_CONTEXT_STORAGE_SIZE` 及其对齐为 v1 预留；应用不得检查或持久化 private bytes。
public enum-like domain 通常使用固定宽度 `int32_t` typedef 与命名常量，避免 native
enum 宽度和 `-fshort-enums` 影响。含 pointer/`size_t` 的结构仍依赖目标架构。

1.0 前，`0.x` minor 之间仍可修正 source/ABI，并记录在 `CHANGELOG.md`；CMake 只把
同一 `0.9.x` minor 视为 package-compatible。1.0 起遵循语义化版本：不兼容 public
C API/ABI 需要新 major；minor 可以新增函数和 enum value，应用的 switch 应保留
`default`。

生命周期和并发规则也是 API：一个 producer 拥有 RX feed/reserve/DMA，一个 consumer
拥有 poll、event release、adapter service 和 reset；event payload 借用到
`wl_event_release()`；sink TX 指针有效到同步完成或匹配的 `wl_tx_complete()`。

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
