# 构建一个 Wirelink Endpoint

本文建立两个内存 endpoint：一个方向发送允许丢失的 telemetry，另一个方向完成可靠的
类型化 RPC。它相当于 libcsp 的 loopback client/server 示例，但抽象不同：Wirelink 是
点对点 link engine，不是带地址的网络或 router。应用组合 endpoint、transport、session
和可选的 WLC 生成 runtime。

> 英文版 [`getting-started.md`](getting-started.md) 是规范来源。

## 心智模型

```text
typed messages and RPC       应用策略
WLC-generated runtime        编码、路由、保留、关联
Wirelink link                成帧、完整性、ACK、重试、去重
port/adapter                  UART、USB、UDP、CAN packet 或测试 loopback
```

一个 consumer 独占发送、poll、event release、transaction completion、runtime service
和 adapter service。transport callback/ISR 可以是唯一 RX producer。Wirelink 不分配
内存，也不创建 thread 或 clock。

| libcsp 概念 | Wirelink 对应概念 |
| --- | --- |
| addressed node | 一个点对点 endpoint；不负责寻址 |
| node incarnation | 可靠流量上的非零 `session_id` |
| destination port | 稳定的 WLC message ID |
| connection-less send | unreliable delivery |
| reliable connection/request | reliable transaction 加可选 RPC |
| interface/driver | 使用 public port API 的 adapter |
| router task | 应用拥有的 poll/service loop |

CRC 只检测意外损坏，不认证 peer。v1 也不提供加密、发现、广播、路由或访问控制；需要时
把 link 放在已认证/加密的 transport 内。

## 构建可运行示例

在源码 checkout 中使用兼容 ABI 18 的 `wlc`：

```sh
cmake -S . -B build/quickstart \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
cmake --build build/quickstart --target wirelink_getting_started
./build/quickstart/examples/wirelink_getting_started
```

预期输出：

```text
unreliable telemetry: sample=7 temperature=23.50 C
reliable RPC: 20 + 22 = 42
```

完整程序是 [`getting_started.c`](../examples/getting_started.c)。内存 sink 代表 packet
transport；移植时只需替换 sink 和 ingress path。

## 1. 定义应用协议

[`quickstart.wl`](../examples/getting_started/quickstart.wl) 为 `Telemetry`、
`AddRequest`、`AddResponse` 分配永久数字 ID；独立的
[`quickstart.bind.wl`](../examples/getting_started/quickstart.bind.wl) 选择 runtime 行为：

```text
latest Telemetry { delivery = unreliable; }

rpc Add {
  request = AddRequest;
  response = AddResponse;
  request_operation_id = operation_id;
  response_operation_id = operation_id;
  response_status = status;
  request_delivery = reliable;
  response_delivery = reliable;
}
```

可替代状态适合 unreliable：新 sample 使旧 sample 失去意义。需要链路确认时使用
reliable；调用方需要应用结果、deadline、status 或有界 duplicate/replay 时使用 RPC。

## 2. 生成并链接类型化 Runtime

使用已安装 Wirelink package 的应用只需要：

```cmake
find_package(Wirelink CONFIG REQUIRED)
wirelink_wlc_generate_codec(
  TARGET quickstart_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/quickstart.wl")
wirelink_wlc_generate_runtime(
  TARGET quickstart_protocol
  CODEC_TARGET quickstart_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/quickstart.bind.wl")
target_link_libraries(my_endpoint PRIVATE quickstart_protocol)
```

codec target 只拥有一份 data model、codec 和 typed send；每个 runtime target 只拥有对应
profile 的 retained/RPC API。这允许同一进程中的 host/device 共用 schema，避免重复
codec symbol 和未使用存储。生成 C 编译进 target；设备上不运行 WLC。

## 3. 初始化 Endpoint 与 Session

双方必须使用相同 envelope、integrity、payload bound 和 transmission-unit bound；v1
通过带外配置这些 profile 值。按照 `wl_config_requirements()` 分配并保持 buffer，然后
调用 `wl_init()`。

session ID 是非零 boot/session incarnation，不是 node address。可靠 DATA/ACK 都携带它，
让 peer 区分重传与重启前流量。从随机 boot nonce 或持久单调 boot counter 产生；旧 frame
仍可能存在时不得复用。

示例使用 native-packet 和固定静态数组。datagram 通过 `wl_feed_unit()` 输入完整 unit；
byte stream 改用 `wl_feed_bytes()` 或 reserve/commit producer API。

## 4. 绑定 Transport

无硬件 bring-up 可绑定 loopback：

```c
wl_loopback_t transport;
wl_loopback_init(&transport, &controller.link, &device.link);

wl_loopback_service_result_t service;
wl_loopback_service(&transport, 4U, &service);
```

adapter 异步借用 encoded unit，在每个方向模拟一个 unit 的背压，并在有界 `service()`
中完成；不增加 payload buffer 或 heap。重初始化任一 link 前先 quiesce。

硬件 adapter 通过 `wl_set_sink()` 注册一个 `wl_sink_fn`：同步消费返回
`WL_SINK_SENT`；异步借用返回 `WL_SINK_STARTED`，之后恰好调用一次
`wl_tx_complete()`；可重试背压返回 `WL_SINK_BUSY`；终态 I/O 失败返回
`WL_SINK_FAILED`。sink 获得的指针到同步返回或异步 completion 前仍归 Wirelink。
RX publish 只使工作 ready；解码与应用 callback 始终留在 consumer。

## 5. 初始化应用 Runtime 存储

只启用 endpoint 实际拥有的角色。controller 示例启用一个 RPC client slot；device 启用
一个 pending operation、一个 replay-cache entry 和 `Add` handler。先调用
`quickstart_runtime_config_defaults()`，再调用 client/server role helper。默认值不会替
产品发明 RPC expiry 策略，需要时显式设置非零 pending/cache timeout。

因为 quickstart RPC payload 都有 schema bound，WLC 会生成
`quickstart_runtime_default_storage_t` 和 descriptor。把 arena 放在 instance 旁边；schema
增长会直接改变 C type，不会悄悄超过猜测的 byte array。更大 slot count 可用
`quickstart_runtime_requirements()` 配置自定义 aligned arena。instance 与 arena 地址必须
保持稳定；pump 也是调用方状态，不是 thread/scheduler。

bring-up 阶段使用 checked init 定位字段或容量错误：

```c
application_runtime_t runtime;
quickstart_runtime_config_t config;
quickstart_runtime_storage_t storage;
quickstart_runtime_init_diagnostic_t diagnostic;

quickstart_runtime_config_defaults(&config);
quickstart_runtime_config_enable_client(&config);
storage = quickstart_runtime_default_storage_descriptor(&runtime.arena);
int rc = quickstart_runtime_init_checked(&runtime.instance, &config, &storage,
                                         &diagnostic);
if (rc != WL_OK) {
  log_init_error(quickstart_runtime_init_issue_str(diagnostic.issue),
                 diagnostic.field, diagnostic.required, diagnostic.provided);
}
```

配置确认后使用普通 `runtime_init()`。配合 function/data section 和 linker GC，未引用的
checked validation 与字符串可从小固件中移除。

## 6. 驱动 Event 与 RPC

构建生成的 application hook，按需填入独立 adapter callback，再执行一个有界 owner pass：

```c
quickstart_runtime_pump_t runtime_pump;
quickstart_runtime_pump_init(&runtime_pump, &runtime.instance.runtime,
                             observe_result, app);
wl_pump_hooks_t hooks = quickstart_runtime_pump_hooks(&runtime_pump);
hooks.adapter_user_data = transport;
hooks.service = transport_service;
hooks.quiesce = transport_quiesce;
hooks.adapter_deadline_hint = transport_deadline;

wl_pump_result_t step;
wl_pump_step(&endpoint.link, now_ms, 16U, &hooks, &step);
```

bridge 共用 pump 的一次时间采样，release RX、回收匹配 RPC terminal、每 pass 最多 service
一个 response，并合并 deadline。成功提交 response 会请求下一次有界 pass；背压等待
transport 推进而不自旋。休眠前调用 `wl_pump_get_hint()`。自定义 owner loop 可直接调用
dispatch/service，并先检查 `event_consumed` 再执行 fallback ownership。

处理结果使用生成 helper，不直接选 diagnostic union：

```c
if (!quickstart_runtime_result_ok(&result)) {
  log_error(quickstart_runtime_result_str(&result));
  return;
}
const quickstart_runtime_rpc_detail_t *rpc =
    quickstart_runtime_result_rpc_detail(&result);
if (rpc != NULL) {
  remember_operation(rpc->operation_id);
}
```

detail tag 不匹配时 accessor 返回 `NULL`。字符串只用于日志；控制流使用 `domain` 或类型化
错误字段。

可靠 server request 还会建立当前 peer session。首次绑定或切换设置
`rpc->peer_changed`，应用 take 一次 observation：

```c
if (rpc != NULL && rpc->peer_changed != 0U) {
  wl_rpc_peer_observation_t peer;
  if (quickstart_runtime_peer_observation_take(
          &runtime.instance.runtime, &peer) == WL_RPC_OK) {
    revoke_old_peer_leases(peer.previous_session_id, peer.current_session_id);
  }
}
```

切换会在新 handler 前清除旧 session 的 pending/cache，并请求取消 detached response。
可靠非 RPC 流量若建立相同产品 session，应在应用它之前显式调用
`quickstart_runtime_peer_observe()`。稳定 session 只做 inline comparison。

RPC 用 `quickstart_add_client_start()` 启动并保存非零 operation ID，终态后
inspect/decode，最后 `quickstart_add_client_release()`。用相同 ID 和 canonical request
重试可命中有界 replay cache；cache eviction、expiry、session change 或 restart 会终止
保护。

## 7. 从 Loopback 移到硬件

保持 schema、runtime、owner loop 和 session 规则不变，只替换 adapter 和 ingress：

- UART/serial stream：COBS envelope 加 byte/DMA publish；
- USB、UDP、packet CAN：native-packet envelope 加 unit publish；
- 自带 16-bit 长度的 bus：`WL_ENVELOPE_BUS_LENGTH16`。

在 core 外启动/停止 adapter；RX、TX completion、writable 时通知 owner；重初始化 endpoint
storage 前 quiesce。

## 8. 可选 Diagnostics

只在需要统一 bring-up 文本的 image 中链接：

```cmake
target_link_libraries(my_endpoint PRIVATE Wirelink::diagnostics)
```

```c
#include <wirelink/diagnostics.h>

char text[256];
wl_rx_counters_t counters;
wl_diag_writer_t writer;

wl_rx_get_counters(&endpoint.link, &counters);
wl_diag_writer_init(&writer, text, sizeof(text));
if (wl_diag_format_rx_counters(&writer, &counters) == WL_OK) {
  product_log(text);
}
```

formatter 不拥有 I/O 或 heap。更多 snapshot 见 [`diagnostics.md`](diagnostics.md)。

## 设计参照

本文采用 libcsp 对 init、buffer、send/receive、interface 和 loopback client/server 的分层
方式。Wirelink 刻意停在 libcsp 的 node address、socket、routing table、router task 和
标准网络 service 之下。
