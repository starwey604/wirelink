# Drive a transport from one owner loop

[中文](tutorial-integration-cn.md) · [Adapter configuration](adapters.md)

Run desktop UDP client/server peers. C business code completes RPC and ordinary telemetry through the same calling owner. The client prints `20 + 22 = 42` and `telemetry sample=...`; the test also checks absent-peer timeout.

From the repository root, provide matching WLC 0.13.0 (contract 0.13), C11/C++20 compilers and standalone Asio headers. Local UDP socket access is required:

```sh
cmake -S . -B /tmp/wirelink-docs-udp -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON -DWIRELINK_BUILD_ASIO_UDP_ADAPTER=ON \
  -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-udp --parallel \
  --target udp_peer_client udp_peer_server
ctest --test-dir /tmp/wirelink-docs-udp --output-on-failure -R '^wirelink_udp_peer$'
```

The test allocates ports, starts three fresh clients reusing the same local address/port, then verifies timeout after server exit. For manual use, run reversed endpoint addresses in two terminals:

```sh
# Terminal 1
/tmp/wirelink-docs-udp/examples/05_zephyr_udp/udp_peer_server 127.0.0.1 49101 127.0.0.1 49100
# Terminal 2
/tmp/wirelink-docs-udp/examples/05_zephyr_udp/udp_peer_client 127.0.0.1 49100 127.0.0.1 49101
```

## Initialize C storage and the adapter

`example_udp_open_at` initializes a C-owned port and two `2053`-byte RX units with fresh platform identity, binds adapter submit/notify, then opens the socket and fixed peer for `2052`-byte maximum datagrams. The generated endpoint then binds route/handlers. The extra RX byte rejects oversized datagrams; it is not another message slot. The helper creates no owner thread or RPC table.

## Carry the business deadline into waiting

The server attempts telemetry every 20 milliseconds. Core timeout, platform clock, adapter wait and business due use milliseconds here, unlike the RAM-loopback tutorials. Each pass services I/O, steps the kernel, then limits waiting by the business due time:

[05_zephyr_udp/server.c](../examples/05_zephyr_udp/server.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "peer_options.h"
int main(int argc, char **argv) {
  static udp_demo_kernel_t server;
  static wl_packet_port_t port;
  static uint8_t rx[2][2053];
  example_udp_t *udp = peer_open(&port, (wl_span_t){&rx[0][0], sizeof(rx)}, sizeof(rx[0]), argc, argv);
  CHECK(udp);
  const udp_demo_kernel_handlers_t handlers = {.on_rpc_add = peer_add};
  CHECK(udp_demo_kernel_init(&server, &port, &handlers, NULL, NULL) == WL_OK);
  puts("UDP peer server ready"); fflush(stdout);
  uint32_t sequence = 0, due = example_now_ms();
  int status = 0;
  while (example_running()) {
    uint32_t now = example_now_ms();
    if (example_udp_step(udp, &server.core, now) != WL_OK) { status = 1; break; }
    if ((int32_t)(now - due) >= 0) {
      telemetry_value_t sample = {.has_sample = true, .sample = ++sequence};
      const wl_err_t error = udp_demo_kernel_send_telemetry(&server, &sample);
      if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) { status = 1; break; }
      due = now + 20;
    }
    now = example_now_ms();
    uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
    if (example_udp_wait(udp, &server.core, wait) != WL_OK) { status = 1; break; }
  }
  if (example_udp_close(udp, &server.core) != WL_OK) status = 1;
  return status;
}
```

## Merge readiness, deadlines and latched notification

These owner helpers are built from [tutorial_host.cpp](../examples/common/tutorial_host.cpp). `example_udp_wait` queries the core deadline and subtracts elapsed work; Asio `wait_for_activity` checks adapter readiness and latched wake internally, combining socket read/write readiness. Do not clear a new notification across query/wait or consume the kernel activity bit from another consumer.

```cpp
wl_err_t example_udp_step(example_udp_t *udp, wl_kernel_t *core, uint32_t now) {
  try {
    const auto error = udp->adapter->service();
    const auto advanced = wl_kernel_step(core, now, 16);
    return error == WL_OK ? advanced : error;
  } catch (...) { return WL_ERR_IO; }
}
wl_err_t example_udp_wait(example_udp_t *udp, wl_kernel_t *core, uint32_t maximum_ms) {
  const uint32_t sampled = example_now_ms();
  int has_deadline = 0; uint32_t deadline = 0;
  if (wl_kernel_ready(core, sampled, &has_deadline, &deadline) || !example_running()) return WL_OK;
  uint32_t limit = maximum_ms;
  if (has_deadline) limit = std::min(limit, deadline - sampled);
  const uint32_t elapsed = example_now_ms() - sampled;
  if (elapsed >= limit) return WL_OK;
  try {
    const auto error = udp->adapter->wait_for_activity(std::chrono::milliseconds(limit - elapsed));
    return error == WL_ERR_NO_DATA ? WL_OK : error;
  } catch (...) { return WL_ERR_IO; }
}
wl_err_t example_udp_close(example_udp_t *udp, wl_kernel_t *core) {
  const auto error = wl_kernel_close(core);
  if (error != WL_OK) return error;
  udp->adapter->stop();
  const auto advanced = wl_kernel_step(core, example_now_ms(), 16);
  if (advanced != WL_OK) return advanced;
  if (!wl_kernel_quiescent(core)) return WL_ERR_BUSY;
  delete udp; return WL_OK;
}
```

## Close and replace the transport

The same owner closes the kernel, stops real socket I/O, steps remaining terminals/completion and requires quiescence before releasing the helper. If you add external notifier/driver workers, also establish their exit before releasing wake context; core quiescence alone is insufficient.

UART/USB use the same kernel with transport-specific service/readiness, FIFO/drain and asynchronous stop/recovery requirements; see [adapters](adapters.md). Zephyr peers share the schema/profile; see [network setup](../samples/zephyr/udp_peer/README.md) and [zephyr_owner.h](../examples/05_zephyr_udp/zephyr_owner.h). For an owner thread and owning client results, use [Session](host-runtime.md) or the [SDK](../examples/06_bindings/GUIDE.md).
