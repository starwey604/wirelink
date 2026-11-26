/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/astrial/packet_serial.hpp"
#include <cassert>
#include "wirelink/node.h"
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <poll.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <termios.h>
#include <unistd.h>
extern "C" const size_t *packet_serial_fifo_layout();

static uint32_t now() {
  return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
int main() {
  const size_t layout_cpp[] = {
      sizeof(wl_byte_fifo_t), alignof(wl_byte_fifo_t), offsetof(wl_byte_fifo_t, read),
      offsetof(wl_byte_fifo_t, write), offsetof(wl_byte_fifo_t, gap),
      offsetof(wl_byte_fifo_t, reserved), offsetof(wl_byte_fifo_t, reservation)};
  assert(!std::memcmp(layout_cpp, packet_serial_fifo_layout(), sizeof(layout_cpp)));
  using namespace std::chrono_literals;
  using wirelink::astrial::PacketSerialAdapter;
  int master = -1, slave = -1;
  char name[256]{};
  assert(openpty(&master, &slave, name, nullptr, nullptr) == 0);
  termios attributes{};
  assert(tcgetattr(slave, &attributes) == 0);
  cfmakeraw(&attributes);
  assert(tcsetattr(slave, TCSANOW, &attributes) == 0);
  close(slave);
  std::atomic<bool> drained{false};
  wirelink::astrial::PacketSerialConfig config;
  config.port = name;
  config.drained = [](void *p) { return static_cast<std::atomic<bool> *>(p)->load() ? 1 : 0; };
  config.drain_user = &drained;
  wl_node_t node{};
  wl_packet_stream_t stream{};
  uint8_t rx[2][106], wire_storage[105], fifo[17];
  const wl_node_stream_ops_t cobs = {wl_packet_stream_cobs_crc16_init,
      wl_packet_stream_cobs_crc16_submit, wl_packet_stream_cobs_crc16_feed,
      wl_packet_stream_abort_rx, wl_packet_stream_notify, WL_NODE_COBS_CRC16};
  wl_node_layout_t layout{};
  layout.instance = 9001;
  layout.rx_storage = rx[0];
  layout.rx_storage_size = sizeof(rx);
  layout.rx_frame_max = layout.tx_frame_max = 100;
  layout.rx_wire_max = layout.tx_wire_max = wl_packet_stream_cobs_crc16_bound(100);
  layout.stream = &stream;
  layout.stream_ops = &cobs;
  layout.stream_tx_wire = {wire_storage, sizeof(wire_storage)};
  layout.has_tx = 1;
  PacketSerialAdapter adapter(config, {fifo, sizeof(fifo)});
  assert(wl_node_bind_transport(&node, &layout, adapter.transport()) == WL_OK);
  auto *port = &node.port;
  uint8_t packet[] = {0x57, 0, 22, 0, 1, 2, 3, 4, 5, 6, 7, 8};
  wl_packet_token_t token{};
  assert(wl_packet_tx_submit(port, packet, sizeof(packet), &token) == WL_OK);
  auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!wl_packet_tx_ready(port)) {
    assert(adapter.service(now()) == WL_OK);
    (void)adapter.wait_for_activity(1ms);
    assert(std::chrono::steady_clock::now() < deadline);
  }
  assert(wl_packet_tx_take(port, &token) == WL_PACKET_CONSUMED && wl_packet_tx_active(port));
  packet[4] = 77; // The selected framer snapshotted the encoded wire before acceptance.
  uint8_t wire[128];
  size_t n = 0;
  do {
    pollfd descriptor{master, POLLIN, 0};
    assert(poll(&descriptor, 1, 1000) == 1);
    assert(read(master, wire + n, 1) == 1);
    ++n;
    assert(n < sizeof(wire));
  } while (wire[n - 1] != 0);
  drained.store(true);
  while (!wl_packet_tx_ready(port)) {
    assert(adapter.service(now()) == WL_OK);
    std::this_thread::sleep_for(1ms);
    assert(std::chrono::steady_clock::now() < deadline);
  }
  assert(wl_packet_tx_take(port, &token) == WL_PACKET_DRAINED);
  assert(write(master, wire, 3) == 3);
  std::this_thread::sleep_for(3ms);
  assert(!wl_packet_rx_ready(port));
  assert(adapter.service(now()) == WL_OK);
  assert(!wl_packet_rx_ready(port));
  assert(write(master, wire + 3, n - 3) == ssize_t(n - 3));
  while (!wl_packet_rx_ready(port)) {
    assert(adapter.service(now()) == WL_OK);
    (void)adapter.wait_for_activity(1ms);
    assert(std::chrono::steady_clock::now() < deadline);
  }
  wl_packet_borrow_t borrow{};
  assert(wl_packet_rx_borrow(port, &borrow) == WL_OK);
  packet[4] = 1;
  assert(borrow.length == sizeof(packet) && !memcmp(borrow.data, packet, sizeof(packet)));
  assert(wl_packet_rx_release(port, &borrow) == WL_OK);
  assert(wl_node_close(&node, now()) == WL_OK);
  assert(wl_packet_quiescent(port) && adapter.errors() == 0);
  // Default FIFO and pipeline: the write callback alone retires each TX lease.
  config.drained = nullptr;
  PacketSerialAdapter pipelined(config);
  ++layout.instance;
  assert(wl_node_bind_transport(&node, &layout, pipelined.transport()) == WL_OK);
  for (int i = 0; i < 2; ++i) {
    assert(wl_packet_tx_submit(port, packet, sizeof(packet), &token) == WL_OK);
    deadline = std::chrono::steady_clock::now() + 2s;
    while (!(wl_packet_tx_take(port, &token) & WL_PACKET_DRAINED)) {
      (void)pipelined.wait_for_activity(1ms);
      assert(std::chrono::steady_clock::now() < deadline);
    }
    assert(!wl_packet_tx_active(port));
  }
  assert(wl_node_close(&node, now()) == WL_OK && pipelined.errors() == 0);
  close(master);
}
