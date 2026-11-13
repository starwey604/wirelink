/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/asio/packet_udp.hpp"
#include "wirelink/packet_stream.h"
#include <cassert>
#include <cstring>
#include <thread>
extern "C" wl_packet_port_t *packet_udp_test_port(unsigned);
extern "C" int packet_udp_test_init(unsigned, wl_packet_sink_fn, wl_packet_notify_fn, void *);
// Activity includes configuration/resource notifications, not just datagrams.
// Keep servicing until the observable receive condition, with one real deadline.
template <typename Predicate>
static void service_until(wirelink::asio::PacketUdpAdapter &adapter, Predicate done) {
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
  using wirelink::asio::PacketUdpAdapter;
  using wirelink::asio::PacketUdpConfig;
  auto *a = packet_udp_test_port(0), *b = packet_udp_test_port(1), *c = packet_udp_test_port(2);
  PacketUdpAdapter aa(*a), bb(*b), cc(*c);
  assert(packet_udp_test_init(0, PacketUdpAdapter::submit, PacketUdpAdapter::notify, &aa) == WL_OK);
  assert(packet_udp_test_init(1, PacketUdpAdapter::submit, PacketUdpAdapter::notify, &bb) == WL_OK);
  assert(packet_udp_test_init(2, PacketUdpAdapter::submit, PacketUdpAdapter::notify, &cc) == WL_OK);
  PacketUdpConfig config;
  config.bind_address = "127.0.0.1";
  config.maximum_datagram_size = 200;
  std::error_code error;
  assert(aa.start(config, error) == WL_OK && bb.start(config, error) == WL_OK &&
         cc.start(config, error) == WL_OK);
  assert(aa.set_peer("127.0.0.1", bb.local_port()) == WL_OK);
  assert(bb.set_peer("127.0.0.1", aa.local_port()) == WL_OK);
  assert(cc.set_peer("127.0.0.1", bb.local_port()) == WL_OK);
  uint8_t message[] = {0xa4, 0, 0, 8, 1, 2, 3};
  wl_packet_token_t token{};
  // A latched wake must not be mistaken for receipt (even on synchronous
  // loopback implementations). No sender has submitted a datagram yet.
  PacketUdpAdapter::notify(&bb);
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
  PacketUdpAdapter::notify(&bb);
  assert(bb.wait_for_activity(100ms) == WL_OK);
  assert(bb.service() == WL_OK);
  assert(bb.wait_for_activity(2ms) == WL_ERR_NO_DATA);
  std::thread producer([&] {
    std::this_thread::sleep_for(5ms);
    PacketUdpAdapter::notify(&bb);
  });
  assert(bb.wait_for_activity(100ms) == WL_OK);
  producer.join();
  aa.stop();
  bb.stop();
  cc.stop();
  assert(wl_packet_quiescent(a) && wl_packet_quiescent(b) && wl_packet_quiescent(c));
  // Explicit COBS framing on the same datagram adapter; no old engine.
  PacketUdpAdapter ab(*a), ba(*b);
  wl_packet_stream_t stream{};
  uint8_t wire[256];
  assert(packet_udp_test_init(0, wl_packet_stream_cobs_crc16_submit, wl_packet_stream_notify,
                              &stream) == WL_OK);
  assert(packet_udp_test_init(1, PacketUdpAdapter::submit, PacketUdpAdapter::notify, &ba) == WL_OK);
  config.decode = wl_packet_cobs_crc16_decode;
  assert(ab.start(config, error) == WL_OK && ba.start(config, error) == WL_OK);
  assert(ab.set_peer("127.0.0.1", ba.local_port()) == WL_OK &&
         ba.set_peer("127.0.0.1", ab.local_port()) == WL_OK);
  wl_packet_stream_config_t sc{};
  sc.port = a;
  sc.tx_storage = {wire, sizeof(wire)};
  sc.maximum_frame_size = 100;
  sc.physical_submit = PacketUdpAdapter::submit;
  sc.notify = PacketUdpAdapter::notify;
  sc.user = &ab;
  assert(wl_packet_stream_cobs_crc16_init(&stream, &sc) == WL_OK);
  assert(wl_packet_tx_submit(a, message, sizeof(message), &token) == WL_OK);
  (void)wl_packet_tx_take(a, &token);
  service_until(ba, [&] { return ba.stats().received == 1; });
  assert(wl_packet_rx_borrow(b, &borrow) == WL_OK && borrow.length == sizeof(message));
  assert(!std::memcmp(borrow.data, message, sizeof(message)));
  assert(wl_packet_rx_release(b, &borrow) == WL_OK);
  ab.stop();
  ba.stop();
  assert(wl_packet_quiescent(a) && wl_packet_quiescent(b));
}
