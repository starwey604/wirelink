# Wirelink 1.0 API 边界

状态：1.0 发布前的设计约定。Compact-v1 的线上字节格式已经冻结；首个
1.0 候选版本之前，C API 和生成 runtime API 仍可调整。本文用于审阅 API
全貌，不逐项罗列所有函数原型。

> 英文版 [`api-boundary.md`](api-boundary.md) 是规范来源。本中文版本用于
> 设计审阅；两者有歧义时以英文版和 public header 为准。

## 建议阅读顺序

第一次接触 Wirelink，请先按场景读三篇教程：[最新温度](getting-started-cn.md)、
[请求一次计算](tutorial-rpc-cn.md)、[接入自己的工程与硬件](tutorial-integration-cn.md)。
下面的顺序用于跑过示例后的 API 审阅，不是入门前置要求。

1. 先读完本文，尤其是末尾的“1.0 前待审阅问题”，判断库边界和所有权模型。
2. 将教程与可编译的 [`latest_telemetry.c`](../examples/latest_telemetry.c) 和
   [`getting_started.c`](../examples/getting_started.c) 对照。
3. 对照阅读 [`adapters-cn.md`](adapters-cn.md) 和
   [`application-layer-cn.md`](application-layer-cn.md)，检查 producer、
   consumer、pump 与关闭流程的划分。
4. 阅读 [WLC 中文指南](https://github.com/starwey604/wlc/blob/31df0e0dae644f380b57e9b2d69a96aa56be0f58/README-cn.md) 和
   [`schema-v1-cn.md`](schema-v1-cn.md)，再查看代表性的生成头文件
   [`control_runtime.h`](../tests/fixtures/wlc/generated/current/control_runtime.h)。
5. 按需阅读策略层：[`latest-mailbox-cn.md`](latest-mailbox-cn.md)、
   [`fifo-cn.md`](fifo-cn.md)、[`rpc-runtime-cn.md`](rpc-runtime-cn.md) 和
   [`bulk-performance-cn.md`](bulk-performance-cn.md)。
6. 最后阅读 [`compatibility-cn.md`](compatibility-cn.md) 和规范性的
   [`protocol-cn.md`](protocol-cn.md)。
   [`api-v1-audit-cn.md`](api-v1-audit-cn.md) 只用于回顾早期决策。

## 一句话边界

Wirelink 是一个无动态分配、由单一 owner 驱动的点对点链路，以及一组可选的
类型化应用 runtime。它负责成帧、完整性校验、链路确认、重试、去重和借用事件；
不负责驱动、线程、堆、时钟、节点地址、路由、认证或产品策略。

普通应用通常只依赖 WLC 生成的 runtime 和 `wirelink/link.h`。适配器实现
`wirelink/port.h`。直接使用 `wirelink/rpc.h` 主要面向生成器和高级集成。

## 可链接 Target

| Target | 用途 | 默认固件代价 |
| --- | --- | --- |
| `Wirelink::wirelink` | C11 链路核心及可选 runtime 原语 | 仅链接器实际选择的核心代码 |
| `Wirelink::loopback` | 内存中的 native-packet 适配器 | 未链接则无代价 |
| `Wirelink::diagnostics` | 向调用方缓冲区写入 key/value 文本 | 未链接则无代价 |
| `Wirelink::host` | 可选的 C++20 线程化主机 executor | 未启用或未链接则无代价 |
| WLC 生成 target | schema codec、bindings 或单个角色 runtime | 仅所选 schema/profile |

Astrial 和 Asio 适配器是源码集成的平台 target，不属于已安装的核心包。

## Header 选择

| Header | 预期使用者 |
| --- | --- |
| `link.h` | endpoint/application owner：初始化、发送、poll、事件、TX 结果 |
| `pump.h` | owner loop 组合与 deadline 合并 |
| `port.h` | adapter/driver producer 与异步 TX 完成通知 |
| `latest.h`、`fifo.h` | lock-free SPSC 保留消息存储 |
| `outbox.h` | 外部串行化、按消息合并的发送队列 |
| `rpc.h` | 底层 RPC 引擎；通常优先使用生成 runtime |
| `bulk.h` | 顺序对象传输的 sender/receiver 状态机 |
| `diagnostics.h` | 可选的 snapshot 与 RPC 结果格式化 |
| `frame.h`、`cobs.h`、`crc.h` | 协议工具、测试及特殊 port |
| `codec.h`、`profile.h`、`span.h`、`types.h` | 公共基础类型 |

`wirelink/wirelink.h` 是同时包含 `link.h` 和 `port.h` 的兼容性 umbrella。
新代码应包含自己实际拥有的窄 header；普通应用不应仅为方便而获得 producer API。

## 核心 Endpoint 生命周期

必须按以下顺序使用：

1. 填写 `wl_config_t`，调用 `wl_config_requirements()`。
2. 分配地址稳定、由调用方拥有的存储，调用 `wl_init()`。
3. 绑定并激活且仅激活一种 adapter/ingress family。
4. 由单一 consumer 发送，并运行 `wl_poll()` 或 `wl_pump_step()`。
5. 每个 RX 事件必须 release 一次；每个可靠 TX 终态 handle 必须 take 一次。
6. 丢弃或复用存储前，先停止 producer 并 quiesce adapter。

核心没有 `deinit()`，因为它不拥有外部资源。资源生命周期在 adapter quiesce
处结束。已初始化的不透明 context 不得复制或移动。

### 配置与 session

通信双方通过带外方式约定 envelope、integrity、payload 上限和 transmission
unit 上限。非零 `session_id` 是可靠流量的启动/实例标识，不是节点地址。
应用从随机数或持久化单调计数器产生它；旧流量仍可能存在时不得复用。

### 发送

`wl_send_unreliable()` 只报告本地提交结果。`wl_send_reliable()` 返回一个
handle，其终态事件表示链路送达或失败，并不表示应用已经执行。
`wl_tx_status()`、`wl_tx_cancel()` 和 `wl_tx_take()` 操作保留的 transaction；
只有 `take()` 会释放终态 slot。

生成的类型化 sender 在内部使用 `wl_tx_payload_claim()`/`commit()`，直接编码到
Wirelink 拥有的存储中；编码失败用 `abort()` 结束 claim。除非数据已经编码，
应用应优先使用生成的类型化操作。

### Poll 与借用事件

`wl_poll()` 执行有界进度并至多返回一个事件。RX payload 一直借用到一次且仅
一次 `wl_event_release()`。可靠终态 handle 一直保留到一次且仅一次
`wl_tx_take()`。`wl_poll_get_hint()` 无副作用，返回是否有立即工作和相对
deadline；adapter 后续产生的外部 wake 仍必须单独处理。

## Port 与 Adapter 边界

adapter 绑定一个 `wl_sink_fn`：

- `WL_SINK_SENT`：字节已同步消费；
- `WL_SINK_STARTED`：adapter 借用字节，之后必须对应调用一次
  `wl_tx_complete()`；
- `WL_SINK_BUSY`：可重试的背压；
- `WL_SINK_FAILED`：不可恢复的本次 I/O 失败。

每个已初始化 context 只能选择一种 RX ingress family：

- 复制式 stream：`wl_feed_bytes()`；
- reserve/direct stream：`wl_rx_reserve()`/`commit()`；
- DMA stream：claim、一次或多次 publish，最后 finish/abort；
- 复制式 packet：`wl_feed_unit()`；
- queued/direct packet：初始化 unit queue，再 claim/commit/abort。

回调和 ISR 只发布字节/完成状态并唤醒 owner；不解码、不调用 handler、不递归发送、
也不运行 `wl_poll()`。统一的 initialize/activate/service/quiesce 映射见
[`adapters-cn.md`](adapters-cn.md)。

## Owner Pump

`wl_pump_step()` 通过彼此独立的 adapter/application context，组合 adapter
service、有界事件分发、应用进度和默认事件清理。事件回调只有在已经 release
RX 事件或 take 终态 handle 后才能返回 `CONSUMED`；返回 `UNHANDLED` 会把该动作
交给 pump。`wl_pump_get_hint()` 合并核心、adapter 和应用的相对 deadline。
`wl_pump_quiesce()` 转交 adapter 的关闭流程。

pump 是同步辅助器，不是 scheduler 或 task。应用仍然拥有 wake 原语、公平性预算、
时钟采样和关闭顺序。

## 应用 Runtime 原语

所有原语都使用调用方存储，并显式转换所有权：

- `LATEST`：三个 SPSC slot；未读状态可以被新值合并；读取是借用。
- `FIFO`：有界 SPSC 顺序队列；满时拒绝新数据；读取是借用。
- `outbox`：外部串行化、每种消息保留最新值，acquire 时复制输出。
- RPC client/server：有界关联、deadline、replay 和 response 所有权；不承诺持久化
  exactly-once。
- Bulk：单个顺序传输及应用状态确认；v1 链路头中没有对象级 staging 或 fragment
  字段。

每个组件都提供 init、生命周期操作、state/stat snapshot；需要时还提供无副作用的
deadline hint。reset 必须由外部串行化，存在 borrow/claim 时不得 reset。

## 默认端点组装

普通应用声明生成的 `*_endpoint_t`，无需自己定义缓冲区容器。它包含通用
`wl_endpoint_t`、消息 runtime 和静态存储，内部成员不是应用 API。
`endpoint_init()` 使用整包传输/CRC32C 默认配置；`init_config()` 允许选择传输、
RPC 角色、超时策略和回调。对象首次使用前必须零初始化，关闭前不能移动。

`endpoint_send_<message>()` 采用 retained profile 的传输方式；
`endpoint_read_<message>()` 复制 LATEST/FIFO 值并内部归还借用。
`endpoint_step()` 推进已连接适配器、消息分发、完成回收和 runtime，具有工作量上限，
不创建线程。`endpoint_handle()` 是适配器入口，`endpoint_runtime()` 提供高级访问。
不要重复处理默认调度已经接管的事件或发送句柄。

容量只根据 profile 选中的消息推导；无关的无界消息不扩大端点。
选中消息无界或超过单帧能力时，`HAS_DEFAULT_ENDPOINT=0`。
更大队列、外部 arena 或 DMA 放置仍走高级自定义存储路径。
详见[设计与限制](default-endpoint-cn.md)。

## WLC 生成接口（ABI 19）

WLC 有意拆分三类职责：

1. `<module>.h/.c`：消息模型、clear/encode/decode、大小上限；
2. `<module>_bindings.h/.c`：类型化直接 router 和类型化发送操作；
3. `<runtime>_runtime.h/.c`：一个 binding profile 选择的 retained/RPC 策略。

同一 codec target 可供多个独立命名的 host/device runtime 共用。生成产物必须同时
匹配 compiler version、codegen ABI、schema identity 和 binding-profile identity。
ABI 19 增加上述默认端点入口；高级 runtime API family 仍包括：

| 类别 | 生成模式 |
| --- | --- |
| 配置 | `*_runtime_config_defaults()`、`*_runtime_config_enable_{client,server}()` |
| 存储 | `*_runtime_requirements()`、可选 `*_default_storage_descriptor()` |
| 初始化 | `*_runtime_init()` 或仅用于 bring-up 的 `*_runtime_init_checked()` |
| 分发 | `*_runtime_dispatch_event()` 及 tagged result helper |
| 保留状态 | 类型化 `*_latest_acquire/release`、`*_fifo_acquire/release` |
| RPC client | 类型化 `*_client_start/inspect/decode/release` |
| RPC server | 类型化 handler 与 `*_server_complete/reject` |
| 推进 | `*_runtime_poll/service/get_deadline_hint` |
| 组合 | `*_runtime_pump_init()` 和 `*_runtime_pump_hooks()` |
| peer 切换 | `*_runtime_peer_observe()` 和 `*_runtime_peer_observation_take()` |

`runtime_init_checked()` 会指出被拒绝的字段和 required/provided capacity。配置经过
验证后，普通固件可使用 `runtime_init()`，让诊断字符串及校验代码被链接器回收。
可靠 server request 会自动观察 peer session，清除旧 session 的 RPC 状态，请求取消
脱离 runtime 的 response，并设置 `detail.rpc.peer_changed`。应用 take 保存的
observation 后撤销产品 lease。可靠的非 RPC 流量若建立相同产品 session，则显式调用
`runtime_peer_observe()`。

生成 result 包含 `domain`、`detail_kind` 和 `event_consumed`。使用
`*_runtime_result_ok()`、`*_result_str()` 和检查 tag 的 detail accessor；不要读取错误的
union member，也不要解析诊断字符串。初始化后应把 `*_runtime_t` 的 wiring 当作
runtime 自有状态，不要直接操作其组件指针。

## Diagnostics 边界

`Wirelink::diagnostics` 将稳定的 `record key=value` 文本追加到
`wl_diag_writer_t`。它不分配内存，也不执行 I/O。截断后仍保留合法、NUL 结尾的前缀，
并记录精确所需字节数。日志、时间戳、severity、snapshot 频率和文本传输均由产品负责。

## 兼容性边界

线上协议、C 库版本、WLC compiler 版本、生成代码 ABI、schema identity 和 profile
identity 是彼此独立的兼容域。Compact-v1 字节向量已经冻结；生成 ABI 要求精确匹配，
所有生成产物应作为一个整体重新生成。

固定宽度的 public enum-like domain 和不透明存储大小计划作为 1.x ABI。
包含 pointer/`size_t` 的结构依赖具体架构。1.x 新能力应通过新函数和独立结构增加，
不应继续向已关闭的 v1 config/event 结构追加字段。参见
[`compatibility-cn.md`](compatibility-cn.md)。

## 1.0 前待审阅问题

以下问题刻意暴露给 API 审阅，而不是藏在文档背后：

- 兼容性 umbrella 是否还应暴露 `port.h`；
- 包括 pump disposition 在内的所有 public enum-like domain，是否都应统一使用显式
  固定宽度 typedef；
- `int`、`wl_err_t`、`wl_rpc_err_t`、`wl_bulk_err_t` 和生成 result domain 的边界是否
  划分正确；
- 生成的 `*_runtime_t` 是否暴露了过多初始化后的 wiring 状态；
- 对固件 include hygiene 而言，一个宽泛的 `diagnostics.h` 是否优于多个窄 formatter
  header；
- 对可靠非 RPC 流量手动执行 peer observation 是否足够易发现，还是应引入统一的
  session object。

这些问题在 1.0 前都可以调整源码 API，而不会改变 Compact-v1 的线上字节格式。
