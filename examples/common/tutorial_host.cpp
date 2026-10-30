/* SPDX-License-Identifier: Apache-2.0 */
#include "tutorial_host.h"
#include "wirelink/asio/packet_udp.hpp"
#include "wirelink/platform.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
struct example_udp { std::unique_ptr<wirelink::asio::PacketUdpAdapter> adapter; };
static volatile std::sig_atomic_t running = 1;
static void stopped(int) { running = 0; }
uint32_t example_now_ms(void) {
  const auto environment = wl_platform_environment();
  return environment.clock.now_ms(environment.clock.user_data);
}
int example_running(void) { return running != 0; }
int example_int32(const char *text, int32_t *value) {
  if (!text || !*text || !value) return 0;
  char *end = nullptr; errno = 0;
  const long long parsed = std::strtoll(text, &end, 10);
  if (errno || *end || parsed < INT32_MIN || parsed > INT32_MAX) return 0;
  *value = static_cast<int32_t>(parsed); return 1;
}
example_udp_t *example_udp_open_at(wl_packet_port_t *port, wl_span_t rx, size_t unit_size,
    const char *local_address, uint16_t local, const char *peer_address, uint16_t peer) {
  if (!port || !local_address || !peer_address || !peer || unit_size < 2053) return nullptr;
  try {
    auto udp = std::make_unique<example_udp_t>();
    udp->adapter = std::make_unique<wirelink::asio::PacketUdpAdapter>(*port);
    uint64_t instance;
    if (wl_session_next(wl_platform_environment().session, 0, &instance) != WL_OK) return nullptr;
    wl_packet_config_t config{};
    config.instance = instance; config.rx_storage = rx.data;
    config.rx_storage_size = rx.length; config.rx_unit_size = unit_size;
    config.submit = wirelink::asio::PacketUdpAdapter::submit;
    config.notify = wirelink::asio::PacketUdpAdapter::notify; config.user = udp->adapter.get();
    if (wl_packet_init(port, &config) != WL_OK) return nullptr;
    wirelink::asio::PacketUdpConfig io;
    io.bind_address = local_address; io.bind_port = local; io.maximum_datagram_size = 2052;
    std::error_code error;
    if (udp->adapter->start(io, error) != WL_OK || udp->adapter->set_peer(peer_address, peer) != WL_OK) {
      std::fprintf(stderr, "UDP open: %s\n", error.message().c_str()); return nullptr;
    }
    std::signal(SIGINT, stopped); std::signal(SIGTERM, stopped);
    return udp.release();
  } catch (const std::exception& error) { std::fprintf(stderr, "UDP open: %s\n", error.what()); return nullptr; }
}
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
