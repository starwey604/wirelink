# 用一个 owner loop 接入传输

[English](tutorial-integration.md) · [Adapter 配置](adapters-cn.md)

运行本机 UDP client/server，让 C 业务代码通过同一个 calling owner 完成 RPC 和普通遥测。成功 client 输出 `20 + 22 = 42` 以及 `telemetry sample=...`；测试还覆盖对端缺席时的 timeout。

在仓库根目录准备匹配 WLC 0.13.0（contract 0.13）、C11/C++20 编译器和 standalone Asio 头文件，需要本机 UDP socket 权限：

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

测试自动分配端口，连续启动三个使用相同本地地址/端口的新 client，再验证 server 停止后 client timeout。手动运行时，在两个终端使用互为反向的地址：

```sh
# Terminal 1
/tmp/wirelink-docs-udp/examples/05_zephyr_udp/udp_peer_server 127.0.0.1 49101 127.0.0.1 49100
# Terminal 2
/tmp/wirelink-docs-udp/examples/05_zephyr_udp/udp_peer_client 127.0.0.1 49100 127.0.0.1 49101
```

## 初始化 C 存储与 adapter

`example_udp_open_at` 用平台新 instance 初始化 C-owned port 和两个 `2053` 字节 RX unit，绑定 adapter submit/notify，再打开最大 `2052` 字节 UDP 包的 socket 和固定 peer。生成端点随后绑定 route/handler。多出的 RX 字节用于拒绝超限 datagram，不是额外消息槽。helper 不创建 owner 线程或 RPC 表。

## 将业务 deadline 带到等待

server 每 20 毫秒尝试一次遥测。core timeout、平台 clock、adapter wait 和业务 due 都在本例使用毫秒；与 RAM-loopback 教程的循环 tick 不同。每轮 service I/O、step kernel，再用业务 due 限制最大等待：

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

## 合并 readiness、deadline 和锁存通知

以下为 [tutorial_host.cpp](../examples/common/tutorial_host.cpp)中实际构建的 owner helper。`example_udp_wait` 查询 core deadline，扣除查询后的耗时；Asio 的 `wait_for_activity` 在内部检查 adapter readiness 和锁存 wake，组合 socket read/write readiness。应用不能在 query/wait 之间清掉新通知，也不应自行消费 kernel 的 activity bit。

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

## 收尾与替换传输

同一 owner close kernel，停止实际 socket I/O，step 交付剩余终态并取得 completion，确认 quiescence 后才释放 helper。若集成外部 notifier/driver worker，还必须先确认它们退出；不能仅凭 core quiescence 释放唤醒上下文。

UART/USB 仍使用同一 kernel，但 service/readiness、FIFO/drain、异步 stop/recovery 各有条件，见 [adapter 指南](adapters-cn.md)。Zephyr 端使用同 schema/profile，见[网络 peer 配置](../samples/zephyr/udp_peer/README.md)及 [zephyr_owner.h](../examples/05_zephyr_udp/zephyr_owner.h)。需要自带 owner 线程和拥有结果的客户端时，用 [Session](host-runtime-cn.md)或 [SDK](../examples/06_bindings/GUIDE.md)。
