# Wirelink 模型、驱动契约与测试体系收敛方案

## 1. 目标与硬约束

### 目标
将 Wirelink 对外心智模型收敛为一条单向直线，消除双向循环接线、内部原语泄露和重复的驱动/测试胶水：

```text
用户层:    wl_node_*  +  <node>_bind / <node>_on_* / <node>_call_* / <node>_send_*
               │  (单向挂载：Node 拥有 Transport，用户不再碰 .core / .port / io / TX_WIRE_MAX)
               ▼
驱动契约:  wl_transport_t  (全仓唯一适配器接口，替代 wl_packet_io_t / ExecutorIo / 手写主循环)
               │
               ▼
内核底座:  wl_endpoint_t + wl_packet_port_t (+ wl_packet_stream_t)  [保留核心算法，退居内部]
```

### 硬约束（不可破坏）
- `src/` 与 `include/wirelink/` 保持纯 ISO C11、零堆分配、无 OS/线程/驱动/系统时钟依赖；`-Wall -Wextra -Wpedantic -Werror` 警告归零。
- 保留 native-only 按需链接（例如 Zephyr 未启用 `CONFIG_WIRELINK_PACKET_STREAM` 时，`src/node.c` 不得引入任何流式帧器符号）。
- 零拷贝与 DMA/USB Bulk 64/512 字节 RX 槽对齐、以及按 schema `RX_FRAME_MAX` 校验超长帧的能力不退化。
- 生成节点采用**显式基类成员**（`&server.node`），不引入隐式宏魔法。
- UDP 适配器对外统一为 **`wl_udp_*`**（`<wirelink/udp.h>`），移除底层实现库名 `asio` 与重复的 C++ `PacketUdpAdapter` 包装，并保留底层系统错误码 `os_error` 供 SDK 使用。
- 开发版本保持 0.0.99，生成契约保持 **`WIRELINK_CODEGEN_CONTRACT = 1`**，两仓同步迁移布局与签名（`wirelink`、`wlc`、`cmake/WirelinkWlc.cmake` 同步）。

---

## 2. 核心设计

### 2.1 唯一驱动契约：`include/wirelink/transport.h`

替代现有的 `wl_packet_io_t`、`ExecutorIo` 以及各适配器各自暴露的 `step_endpoint` / `wait_endpoint` / `close_endpoint`：

```c
typedef struct wl_transport wl_transport_t;

typedef struct {
  uint8_t *rx_storage;
  size_t rx_storage_size;
  size_t rx_unit_size;
} wl_transport_rx_override_t;

typedef struct {
  wl_packet_port_t *port;
  wl_packet_stream_t *stream; /* Non-NULL only for stream-framed nodes (COBS/length16). */
  size_t rx_wire_max;
  size_t tx_wire_max;
} wl_transport_attach_t;

typedef struct {
  /* Optional: supply external RX slots before port init (e.g. USB Bulk 64/512B alignment). */
  wl_err_t (*configure_rx)(wl_transport_t *self, size_t min_unit_size,
                           wl_transport_rx_override_t *out);
  /* Bind initialized port/stream and start I/O. Called once by wl_node after port init.
   * Must roll back any partially registered callbacks/resources before returning an error. */
  wl_err_t (*attach)(wl_transport_t *self, const wl_transport_attach_t *info);
  /* TX submission (may be NULL only for recv-only transports). */
  wl_packet_submit_t (*submit)(wl_transport_t *self, const uint8_t *data, size_t length,
                               wl_packet_token_t token);
  /* Optional thread-safe wake from I/O or external producer. */
  void (*notify)(wl_transport_t *self);
  /* Optional owner-side pump (NULL for pure IRQ/background-thread transports). */
  wl_err_t (*service)(wl_transport_t *self, uint32_t now_ms);
  /* Optional readiness & blocking wait (NULL for bare-metal polling). */
  wl_io_readiness_t (*readiness)(const wl_transport_t *self, uint32_t now_ms);
  wl_err_t (*wait)(wl_transport_t *self, uint32_t max_ms);
  /* Request stop and report completion:
   * - WL_OK: RX/TX producers stopped and all async callbacks/buffers reclaimed.
   * - WL_ERR_WOULD_BLOCK: stop initiated; owner must keep stepping/retrying close until WL_OK.
   * - Other errors (e.g. WL_ERR_IO): hardware disable failure, propagated to caller. */
  wl_err_t (*stop)(wl_transport_t *self);
} wl_transport_ops_t;

struct wl_transport {
  const wl_transport_ops_t *ops;
};
```

**关键契约**：
- **消除循环依赖与初始化顺序地雷**：`wl_node` 先可选调用 `configure_rx`，再初始化 `port`/`stream`/`endpoint`，最后调用 `attach`。若 `attach` 返回错误，驱动必须自行撤销已注册回调；`wl_node` 随后调用 `wl_packet_stop(&node->port)` 并清空 `node->transport`，不留半初始化状态。
- **区分“请求停止”与“可释放存储”**：支持同步立停驱动（UDP 直接返回 `WL_OK`）与异步归还缓冲/可失败驱动（Zephyr USB 返回 `WL_ERR_IO` / `WL_ERR_WOULD_BLOCK`；UART DMA 在 `stop` 后由 owner 继续推进直到缓冲回收完毕）。
- **自动传递 `tx_wire_max`**：UDP 驱动在 `attach` 中默认采用 `info->tx_wire_max`，用户无需手填 `SERVER_TX_WIRE_MAX`。

### 2.2 统一节点运行时：`include/wirelink/node.h` + `src/node.c`

所有 WLC 生成节点结构体以 `wl_node_t node;` 作为**第一个成员**，并由生成代码在流式节点上注入轻量 `stream_ops` 虚表（Native 节点传 `NULL`），保证 `src/node.c` **零符号依赖 `packet_stream`**：

```c
typedef struct wl_node wl_node_t;
typedef wl_err_t (*wl_node_sync_tx_fn)(wl_node_t *node, const uint8_t *data, size_t length);

/* Supplied by WLC only for stream-framed nodes; NULL for native-only builds. */
typedef struct {
  wl_err_t (*init)(wl_packet_stream_t *, const wl_packet_stream_config_t *);
  wl_packet_submit_t (*submit)(void *, const uint8_t *, size_t, wl_packet_token_t);
  wl_err_t (*feed)(wl_packet_stream_t *, const uint8_t *, size_t, size_t *);
  wl_err_t (*abort_rx)(wl_packet_stream_t *);
  wl_packet_notify_fn notify;
} wl_node_stream_ops_t;

struct wl_node {
  wl_endpoint_t core;
  wl_packet_port_t port;
  wl_transport_t *transport;
  wl_packet_stream_t *stream;
  const wl_node_stream_ops_t *stream_ops;
  wl_node_sync_tx_fn sync_tx;
  void *user;
  wl_endpoint_error_fn on_error;
  uint16_t rx_wire_max;
  uint16_t tx_wire_max;
  uint8_t transport_stopped; /* 1 when no transport or transport->ops->stop returned WL_OK */
};

/* 面向应用层的唯一生命周期 API（全仓固定 wl_node_* 前缀） */
wl_err_t wl_node_step(wl_node_t *node, uint32_t now_ms);
wl_err_t wl_node_wait(wl_node_t *node, uint32_t now_ms, uint32_t max_wait_ms);
wl_err_t wl_node_close(wl_node_t *node, uint32_t now_ms);
int wl_node_quiescent(const wl_node_t *node);
void wl_node_tx_ready(wl_node_t *node);
wl_err_t wl_node_cancel(wl_node_t *node, wl_call_handle_t handle);

/* 裸机/自定义驱动喂入接口（自动校验 native vs stream，误调返回 WL_ERR_INVALID_STATE） */
wl_err_t wl_node_rx_packet(wl_node_t *node, const uint8_t *data, size_t length);
wl_err_t wl_node_rx_feed(wl_node_t *node, const uint8_t *data, size_t length, size_t *consumed);

/* 供 WLC 生成代码调用的内部装配描述 */
typedef struct {
  uint32_t instance; /* 0 = automatic */
  wl_endpoint_storage_t storage;
  wl_endpoint_route_fn route;
  uint8_t *rx_storage;
  size_t rx_storage_size;
  uint16_t rx_frame_max; /* Protocol-level RX cap (enforced even with larger USB RX slots) */
  uint16_t tx_frame_max;
  uint16_t rx_wire_max;  /* Physical wire RX minimum bound (slot >= rx_wire_max + 1) */
  uint16_t tx_wire_max;
  uint8_t has_tx;
  wl_packet_stream_t *stream;
  const wl_node_stream_ops_t *stream_ops; /* NULL for native framing */
  wl_span_t stream_tx_wire;
  void *user;
} wl_node_layout_t;

wl_err_t wl_node_bind_transport(wl_node_t *node, const wl_node_layout_t *layout,
                                wl_transport_t *transport);
wl_err_t wl_node_bind_sync(wl_node_t *node, const wl_node_layout_t *layout,
                           wl_node_sync_tx_fn tx_write);
```

**核心行为**：
- **分离 `step` 与 `wait`（不提供易误用的单时间戳 `poll`）**：调用方先 `wl_node_step(&n.node, now_ms())`，再重新采样时间调用 `wl_node_wait(&n.node, now_ms(), max_ms)`，避免用户回调耗尽 RPC deadline 后仍按旧时间戳睡眠，同时严格遵守 C core 不读系统时钟。
  - `wl_node_step`：`transport->ops->service`（如有）→ `wl_endpoint_step(&node->core, now_ms, WL_ENDPOINT_BUDGET)`。
  - `wl_node_wait`：合并 `wl_endpoint_ready` 与 `transport->ops->readiness`，无可执行项时等待 `min(max_wait_ms, endpoint_remaining, io.next_deadline_ms)`（将 `WL_ERR_NO_DATA` 归一化为 `WL_OK`）。
- **安全关闭与可释放判定（`wl_node_close` / `wl_node_quiescent`）**：
  1. 幂等调用 `wl_endpoint_close(&node->core)`；
  2. 若有 `transport && !node->transport_stopped`：调用 `stop_err = transport->ops->stop(transport)`：
     - `WL_OK`：置 `node->transport_stopped = 1`；
     - `WL_ERR_WOULD_BLOCK`：推进 `service` + `wl_endpoint_step` 后返回 `WL_ERR_WOULD_BLOCK`（供 UART DMA 等驱动在 owner 推进下归还缓冲，调用方继续循环 `wl_node_close` 直到返回 `WL_OK`）；
     - 其他错误（如 `WL_ERR_IO`）：直接返回错误，不得标记停稳；
  3. 当 `node->transport_stopped == 1`（或同步裸机模式下调用方已停掉 RX 生产者）且 `node->stream_ops != NULL`：调用 `node->stream_ops->abort_rx(node->stream)` 释放半帧占用的 RX 槽；
  4. 调用 `wl_endpoint_step(&node->core, now_ms, WL_ENDPOINT_BUDGET)`；
  5. `wl_node_quiescent(node)` 仅在 `node->transport_stopped && wl_endpoint_quiescent(&node->core)` 时返回真；`wl_node_close` 在未 quiescent 时返回 `WL_ERR_BUSY`。
- **流式解码协议上限**：初始化 `wl_packet_stream_config_t` 时显式设置 `maximum_frame_size = layout->tx_frame_max` 与 `maximum_rx_frame_size = layout->rx_frame_max`，即使 USB 提供了更大的物理 RX 槽，仍按 schema 的 `RX_FRAME_MAX` 拒收超长帧。
- **C++20 直调**：`wl_node_t` 全部原子访问封装在 `src/node.c` / `src/packet_port.c`，C++ 可直接包含生成头文件调用 `<node>_bind` 与 `wl_node_*`，消除跨语言 `*_owner.c` 胶水。

### 2.3 WLC 生成代码瘦身（`wlc/src/node_codegen.rs` & `wlc/src/sdk_codegen/cpp.rs`）

以 `node server { serve Add; }` 为例：
- **结构体**：`struct server_node { wl_node_t node; /* 私有静态缓冲区 */ };`
- **保留的节点前缀符号（仅装配 + 业务）**：
  - `<node>_bind(&self, transport [, &handlers, user])`（默认 `instance = 0`）
  - `<node>_bind_instance(&self, instance, transport [, &handlers, user])`（持久化链路/Session）
  - `<node>_bind_sync(&self [, tx_write] [, &handlers, user])`（裸机默认 `instance = 0`）
  - `<node>_bind_sync_instance(&self, instance [, tx_write] [, &handlers, user])`（同步持久链路显式 instance）
  - 业务符号：`<node>_on_<rpc/msg>`、`<node>_call_<rpc>`（含 `_ex`/`_start`）、`<node>_send_<msg>`（含 `_val`）
- **删除的重复生成符号**：`<node>_init`、`<node>_init_with_instance`、`<node>_init_io`、`<node>_step`、`<node>_close`、`<node>_quiescent`、`<node>_rx_packet`、`<node>_rx_feed`、`<node>_tx_ready`、`<node>_cancel`。
- **`sdk_codegen/cpp.rs`**：改调 `<node>_bind_instance`，向 `Session` 暴露 `wl_node_t *`。

### 2.4 UDP 适配器收敛：`adapters/asio_udp` → `<wirelink/udp.h>`（`wl_udp_*`）

保留 Asio 内核，对外仅提供一套 C11 API（删除重复的 `packet_udp.h` 与 `packet_udp.hpp`）：

```c
typedef struct wl_udp wl_udp_t;

typedef struct {
  const char *bind_address;        /* NULL -> "0.0.0.0" */
  uint16_t bind_port;              /* 0 -> OS ephemeral port */
  const char *peer_address;        /* Optional, paired with peer_port */
  uint16_t peer_port;
  size_t maximum_datagram_size;    /* 0 -> auto from attached node rx_wire_max */
  size_t maximum_tx_datagram_size; /* 0 -> auto from attached node tx_wire_max */
  unsigned service_budget;         /* 0 -> default 8 (1..16) */
  bool learn_peer;
  wl_err_t (*decode)(uint8_t *, size_t, size_t *); /* Optional in-place framing decode */
} wl_udp_config_t;

typedef struct {
  uint64_t received, sent, rejected, errors;
} wl_udp_stats_t;

/* Opens and binds socket immediately so wl_udp_local_port works even before bind().
 * On failure, writes platform errno/WSAGetLastError to *os_error when non-NULL. */
wl_err_t wl_udp_open(wl_udp_t **out, const wl_udp_config_t *config, int *os_error);
wl_transport_t *wl_udp_transport(wl_udp_t *udp);
void wl_udp_close(wl_udp_t *udp);

wl_err_t wl_udp_set_peer(wl_udp_t *udp, const char *address, uint16_t port);
uint16_t wl_udp_local_port(const wl_udp_t *udp);
wl_udp_stats_t wl_udp_stats(const wl_udp_t *udp);
```

- CMake 导出 `Wirelink::udp`（`Wirelink::asio_udp` 视安装消费者需要可同步切换为 `Wirelink::udp`）。
- `runtime/host/Executor` 直接接收 `wl_node_t &node` 与 `wl_clock_t clock`，保持“每次 `step` 后重新读取 `clock` 再调用 `wl_node_wait`”的既有 deadline 语义，**整块删除 `ExecutorIo`**。
- `bindings/cpp/Session` 直接持有 `wl_udp_t *`（RAII 封装）与 `wl_node_t *`，将 `wl_udp_open(..., &os_err)` 写入 `Error::transport`，保持 Python SDK `test_open_rollback`（`os_error != 0`）语义不变。

---

## 3. 测试体系重构与冗余清理（先迁承接点，再删旧目录）

将分散、重叠且含僵尸目录的测试收拢为 **4 层职责单一的金字塔**：

| 层级 | 目录 | 唯一职责 | 收敛动作（严格先迁后删） |
| :--- | :--- | :--- | :--- |
| **1. Core & Node 单测** | `tests/core/` | 协议帧、编解码、`packet_port`、`packet_stream`、`byte_fifo`、`endpoint`、`wl_node` 与 WLC 生成代码 | 合并 `generated_usb`、`generated_loopback`、`generated_instrument`、`endpoint_cpp`、`tests/session`、`tests/usb_sample`、`tests/packet_tools` 为聚焦的 `node_test.c` + `generated_node_test.c` + `node_cpp_test.cpp`（同步承接两项回归：① 原 `examples/01_rpc/lifecycle.c` 的生命周期断言；② 原 `tests/clock_bridge` 的跨 `uint32_t` 回绕 RPC 超时回归）；保留 `wire`、`codec_required`、`packet_port`、`packet_port_concurrent`、`packet_stream`、`byte_fifo`、`endpoint`。 |
| **2. Adapter / Runtime / SDK** | `tests/core/`、`runtime/host/tests/`、`bindings/cpp/tests/`、`examples/06_bindings`、`examples/07_device_sdk` | 每个适配器 1 份边界测试 + Executor/Session + 生成 SDK 闭环 | `packet_udp_test` 改为直测 `wl_udp_*`（删 `packet_udp_owner.c`）；`runtime/host/tests` 与 `bindings/cpp/tests` 去掉手工 `ExecutorIo` 与 `owner.c`；先将 `bindings/cpp/tests/CMakeLists.txt` 所用 schema 夹具迁入 `tests/fixtures/` 再删旧目录；保留 `06_bindings` / `07_device_sdk` C++ 与 Python pytest。 |
| **3. Package 打包冒烟** | `tests/package/` | 验证 `cmake --install`、`find_package(Wirelink)`、`pkg-config`、`-fshort-enums`、WLC CMake 与 SDK 消费 | **先将 `tests/package/static_endpoint/` 共享 schema 夹具统一迁入 `tests/fixtures/static_endpoint/`**，再将 10 个子目录收敛为 3 个：`installed_core/`（合并原 `c_core` + `consumer` + `udp_consumer` + `wlc_current`）、`calculator_consumer/`、`device_consumer/`。<br>承接完成后删除：`tests/package/static_kernel/`、`static_endpoint/`、`cpp_session/`、`packet_adapters/`、`missing_backend/`、`tests/clock_bridge/`、`tests/tutorials/loopback/`、`tests/tutorials/device_service.py`、`tests/tutorials/udp_processes.py`。将 `esp32_uart_timeout*` 与 `zephyr_dwc2_completion*` 移入 `tests/zephyr/host_fixtures/`（如无外部引用可同步清理）。 |
| **4. Examples / Zephyr / WLC** | `examples/`、`tests/zephyr/`、`wlc/tests/` | 示例仅做编译验证 + 文档同步；Zephyr Twister 与 WLC Rust 测试分别守护嵌入式与编译器 | 先将 `examples/01_rpc/lifecycle.c` 中的 `[docs:calculator_cancel_timeout]` 锚点迁至 `examples/01_rpc/client_async.c` 并更新 `tests/tutorials/doc_sources.py` 映射，再移除 `lifecycle.c`；`00_telemetry`、`01_rpc`、`02_device_info` 仅构建验证；保留 `wirelink_tutorial_doc_sources`、`bare_metal_loopback`、`05_zephyr_udp` 跨端冒烟、Zephyr Twister 与 `wlc` Cargo 测试。 |

---

## 4. 两阶段执行清单与双仓切换

### Phase 1：核心骨架 + WLC + UDP + Host/Bindings + Examples + Benchmarks + 测试瘦身
1. **Core**：新增 `include/wirelink/transport.h`、`include/wirelink/node.h`、`src/node.c`（同步加入 `zephyr/CMakeLists.txt` 的 `CONFIG_WIRELINK` 核心源列表，且不依赖 `packet_stream`）；保持 `WIRELINK_CODEGEN_CONTRACT` 为 `1`（同步 `cmake/WirelinkWlc.cmake`）。
2. **WLC (`wlc/`)**：重构 `node_codegen.rs` 与 `sdk_codegen/cpp.rs`（输出 `wl_node_t node;` 首成员、`rx_frame_max`、`stream_ops`、`bind*`）；同步更新 `wlc/tests/`。
3. **UDP / Runtime / Bindings / Benchmarks**：
   - `adapters/asio_udp` 收敛为 `<wirelink/udp.h>`（`wl_udp_*`），实现 `wl_transport_t`，删除旧 `packet_udp.h` / `packet_udp.hpp`；
   - `runtime/host/Executor` 迁移到 `wl_node_t`，删除 `ExecutorIo`；
   - `bindings/cpp/Session` 迁移到 `wl_udp_*`（传递 `os_error`）与 `wl_node_t`；
   - 同步迁移 CI 与基准目标：`benchmarks/owner_pass/`、`benchmarks/api/fixture.c`、`benchmarks/notification_progress/census.c`。
4. **Examples**：迁移 `00_telemetry`、`01_rpc`、`02_device_info`、`03_device_service`、`04_composed_services`、`05_zephyr_udp`、`06_bindings`、`07_device_sdk`、`bare_metal_loopback` 至 `wl_node_*` + `wl_udp_*`（重新生成 `06_bindings` / `07_device_sdk`）。
5. **测试收敛与 CI 更新**：按第 3 节先承接夹具/锚点/回绕用例再删旧目录，同步 `.github/workflows/host.yml` 与 `bindings.yml`。

**新节点 API 双仓切换顺序**：
1. 本地同步完成 `wlc/` 与 `wirelink` 修改并全量验收通过；
2. 先推送 `wlc/` 的新生成器提交并取得 SHA；
3. 更新 `wirelink/.github/actions/setup-wlc/action.yml` 默认 ref（或同步合并两仓 `main`）以及 `wlc/.github/workflows/ci.yml` 引用的 Wirelink ref，让 CI 使用匹配新节点布局的提交。

**Phase 1 本地验收命令**（涵盖真实 Host CI 选项与文档同步基线）：
```sh
cargo test --manifest-path wlc/Cargo.toml
cargo clippy --manifest-path wlc/Cargo.toml --all-targets --all-features -- -D warnings
cargo build --release --locked --manifest-path wlc/Cargo.toml

cmake -S . -B build/verify -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_BUILD_CPP_BINDINGS=ON -DWIRELINK_BUILD_OWNER_PASS_TESTS=ON \
  -DWIRELINK_WLC_EXECUTABLE=$(pwd)/wlc/target/release/wlc \
  -DWIRELINK_ASIO_INCLUDE_DIR=<asio_include_dir>
cmake --build build/verify --parallel
ctest --test-dir build/verify --output-on-failure
python3 tests/tutorials/doc_sources.py
```

### Phase 2：硬件适配器收口 + 清除旧胶水 + 刷新文档站
1. 将 `adapters/zephyr/udp`、`adapters/zephyr/uart_{dma,irq,poll}`、`adapters/zephyr/usb_bulk`、`adapters/astrial`、`adapters/astrial_usb` 接入 `wl_transport_t`（USB Bulk 使用 `configure_rx` 提供 64/512B 对齐槽，并在 `stop` 中正确返回 `WL_ERR_IO` / `WL_ERR_WOULD_BLOCK`；UART DMA 在 `stop` 返回 `WL_ERR_WOULD_BLOCK` 期间允许 owner 持续推进至缓冲归还）。
2. 迁移 `samples/zephyr/` 与 `tests/zephyr/` 到 `wl_node_*`，删除 `examples/05_zephyr_udp/zephyr_owner.h`、`examples/common/packet_tool_owner.c`、`wl_packet_io_t`（以及不再需要的 `wl_waiter_t` 公共胶水）。
3. 验证 `west twister`（`unit_testing` / `native_sim` / QEMU，含 `static_usb_bulk` 停机回归与 COBS/length16 半帧关闭回归）和 Astrial 适配器测试通过。
4. 同步刷新中文文档站（`site/src/content/docs/zh-cn/`）并验证 `doc_sources.py`、`npm --prefix site run check:docs`、`npm --prefix site run build`。
