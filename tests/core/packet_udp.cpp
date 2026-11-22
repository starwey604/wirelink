/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/udp.h"
#include "wirelink/node.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

struct Peer {
  wl_node_t node{};
  wl_packet_stream_t stream{};
  uint8_t rx[2][256]{}, tx_wire[256]{};
  wl_udp_t *udp{};
  wl_transport_t *transport{};
  explicit Peer(const wl_udp_config_t &config, bool cobs = false) {
    assert(wl_udp_open(&udp, &config, nullptr) == WL_OK && wl_udp_local_port(udp));
    transport = wl_udp_transport(udp);
    wl_node_layout_t layout{};
    layout.rx_storage = &rx[0][0];
    layout.rx_storage_size = sizeof(rx);
    layout.rx_frame_max = 200;
    layout.rx_wire_max = 200;
    layout.tx_frame_max = 250;
    layout.tx_wire_max = 250;
    layout.has_tx = 1;
    static const wl_node_stream_ops_t cobs_ops{
      wl_packet_stream_cobs_crc16_init, wl_packet_stream_cobs_crc16_submit,
      wl_packet_stream_cobs_crc16_feed, wl_packet_stream_abort_rx, wl_packet_stream_notify,
      WL_NODE_COBS_CRC16
    };
    if (cobs) {
      layout.stream = &stream;
      layout.stream_ops = &cobs_ops;
      layout.stream_tx_wire = {tx_wire, sizeof(tx_wire)};
      const auto rx_bound = wl_packet_stream_cobs_crc16_bound(layout.rx_frame_max);
      const auto tx_bound = wl_packet_stream_cobs_crc16_bound(layout.tx_frame_max);
      assert(rx_bound <= sizeof(rx[0]) && tx_bound <= sizeof(tx_wire));
      layout.rx_wire_max = static_cast<uint16_t>(rx_bound);
      layout.tx_wire_max = static_cast<uint16_t>(tx_bound);
    }
    assert(wl_node_bind_transport(&node, &layout, transport) == WL_OK);
  }
  ~Peer() {
    assert(wl_node_close(&node, 0) == WL_OK && wl_node_quiescent(&node));
    wl_udp_close(udp);
  }
  wl_err_t service() { return transport->ops->service(transport, 0); }
  void notify() { transport->ops->notify(transport); }
  wl_err_t wait_for_activity(std::chrono::milliseconds limit) {
    return transport->ops->wait(transport, static_cast<uint32_t>(limit.count()));
  }
  bool ready() { return transport->ops->readiness(transport, 0).runnable != 0; }
  wl_udp_stats_t stats() const { return wl_udp_stats(udp); }
  void connect(Peer &other) {
    assert(wl_udp_set_peer(udp, "127.0.0.1", wl_udp_local_port(other.udp)) == WL_OK);
  }
};
// Activity includes configuration/resource notifications, not just datagrams.
// Keep servicing until the observable receive condition, with one real deadline.
template <typename Predicate>
static void service_until(Peer &adapter, Predicate done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (;;) {
    assert(adapter.service() == WL_OK);
    if (done()) return;
    const auto now = std::chrono::steady_clock::now();
    assert(now < deadline);
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    const auto status = adapter.wait_for_activity(remaining);
    assert(status == WL_OK || status == WL_ERR_NO_DATA);
  }
}
int main() {
  using namespace std::chrono_literals;
  wl_udp_config_t config{};
  config.bind_address = "127.0.0.1";
  wl_udp_t *bad = nullptr;
  int os_error = -1;
  config.peer_port = 7;
  assert(wl_udp_open(&bad, &config, &os_error) == WL_ERR_INVALID_ARG && !bad && os_error == 0);
  config.peer_address = "invalid";
  assert(wl_udp_open(&bad, &config, nullptr) == WL_ERR_INVALID_ARG && !bad);
  config.peer_address = nullptr;
  config.peer_port = 0;
  Peer aa(config), bb(config), cc(config);
  config.bind_port = wl_udp_local_port(aa.udp);
  assert(wl_udp_open(&bad, &config, &os_error) == WL_ERR_IO && !bad && os_error);
  config.bind_port = 0;
  aa.connect(bb); bb.connect(aa); cc.connect(bb);
  auto *a = &aa.node.port, *b = &bb.node.port, *c = &cc.node.port;
  uint8_t message[] = {0x57, 0, 8, 0, 1, 2, 3};
  wl_packet_token_t token{};
  // A latched wake must not be mistaken for receipt (even on synchronous
  // loopback implementations). No sender has submitted a datagram yet.
  bb.notify();
  assert(bb.wait_for_activity(100ms) == WL_OK);
  assert(bb.service() == WL_OK && !wl_packet_rx_ready(b) && bb.stats().received == 0);
  // Foreign source is consumed but never published.
  assert(wl_packet_tx_submit(c, message, sizeof(message), &token) == WL_OK);
  (void)wl_packet_tx_take(c, &token);
  service_until(bb, [&] { return bb.stats().rejected == 1; });
  assert(!wl_packet_rx_ready(b));
  // Two RX units and a retained borrow block a third datagram until release.
  for (unsigned i = 0; i < 3; ++i) {
    assert(wl_packet_tx_submit(a, message, sizeof(message), &token) == WL_OK);
    (void)wl_packet_tx_take(a, &token);
  }
  service_until(bb, [&] { return bb.stats().received == 2; });
  wl_packet_borrow_t borrow{};
  assert(wl_packet_rx_borrow(b, &borrow) == WL_OK && borrow.length == sizeof(message));
  assert(!std::memcmp(borrow.data, message, sizeof(message)) && !bb.ready());
  assert(bb.service() == WL_OK && borrow.data[4] == 1);
  assert(wl_packet_rx_release(b, &borrow) == WL_OK);
  service_until(bb, [&] { return bb.stats().received == 3; });
  for (unsigned i = 0; i < 2; ++i) {
    assert(wl_packet_rx_borrow(b, &borrow) == WL_OK);
    assert(wl_packet_rx_release(b, &borrow) == WL_OK);
  }
  assert(bb.stats().received == 3);
  // Drain old latches; an idle receive reservation must not wake itself forever.
  assert(bb.service() == WL_OK);
  for (unsigned i = 0; i < 3; ++i)
    (void)bb.wait_for_activity(0ms);
  assert(bb.service() == WL_OK && bb.wait_for_activity(2ms) == WL_ERR_NO_DATA);
  bb.notify();
  assert(bb.wait_for_activity(100ms) == WL_OK);
  assert(bb.service() == WL_OK);
  assert(bb.wait_for_activity(2ms) == WL_ERR_NO_DATA);
  std::thread producer([&] {
    std::this_thread::sleep_for(5ms);
    bb.notify();
  });
  assert(bb.wait_for_activity(100ms) == WL_OK);
  producer.join();
  // Independent RX and TX limits let this sender submit a frame above its RX maximum.
  uint8_t large[235]{};
  assert(wl_packet_tx_submit(a, large, sizeof(large), &token) == WL_OK);
  assert(wl_packet_tx_take(a, &token) & WL_PACKET_DRAINED);
  service_until(bb, [&] { return bb.stats().rejected == 2; });

  config.decode = wl_packet_cobs_crc16_decode;
  Peer ab(config, true), ba(config, true);
  ab.connect(ba); ba.connect(ab);
  a = &ab.node.port; b = &ba.node.port;
  assert(wl_packet_tx_submit(a, message, sizeof(message), &token) == WL_OK);
  (void)wl_packet_tx_take(a, &token);
  service_until(ba, [&] { return ba.stats().received == 1; });
  assert(wl_packet_rx_borrow(b, &borrow) == WL_OK && borrow.length == sizeof(message));
  assert(!std::memcmp(borrow.data, message, sizeof(message)));
  assert(wl_packet_rx_release(b, &borrow) == WL_OK);
}
