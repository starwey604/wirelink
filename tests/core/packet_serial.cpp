/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/astrial/packet_serial.hpp"
#include <cassert>
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
extern "C" wl_packet_port_t *packet_serial_test_port();
extern "C" wl_byte_fifo_t *packet_serial_test_fifo();
extern "C" wl_packet_stream_t *packet_serial_test_stream();
extern "C" int packet_serial_test_init(wl_packet_sink_fn, wl_packet_notify_fn, void *);
static uint32_t now() {
  return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
int main() {
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
  auto *port = packet_serial_test_port();
  PacketSerialAdapter adapter(*port, *packet_serial_test_fifo(), *packet_serial_test_stream());
  assert(packet_serial_test_init(PacketSerialAdapter::submit, PacketSerialAdapter::notify,
                                 &adapter) == WL_OK);
  std::atomic<bool> drained{false};
  wirelink::astrial::PacketSerialConfig config;
  config.port = name;
  config.drained = [](void *p) { return static_cast<std::atomic<bool> *>(p)->load() ? 1 : 0; };
  config.drain_user = &drained;
  std::error_code error;
  assert(adapter.start(config, error) == WL_OK);
  uint8_t packet[] = {0xa4, 0, 0, 22, 1, 2, 3, 4, 5, 6, 7, 8};
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
  adapter.stop();
  assert(wl_packet_quiescent(port) && adapter.errors() == 0);
  close(master);
}
