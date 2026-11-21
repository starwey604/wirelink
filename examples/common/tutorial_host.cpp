/* SPDX-License-Identifier: Apache-2.0 */
#include "tutorial_host.h"
#include "wirelink/asio/packet_udp.hpp"
#include "wirelink/platform.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
struct example_udp {
  wirelink::asio::PacketUdpAdapter adapter;
  wl_packet_io_t io;
  explicit example_udp(wl_packet_port_t &port) : adapter(port), io(adapter.io()) {}
};
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
wl_err_t example_instance(uint32_t *instance) {
  return wl_session_next(wl_platform_environment().session, 0, instance);
}

example_udp_t *example_udp_create(wl_packet_port_t *port) {
  if (!port) return nullptr;
  try { return new example_udp(*port); }
  catch (...) { return nullptr; }
}

const wl_packet_io_t *example_udp_io(const example_udp_t *udp) { return &udp->io; }
void example_udp_destroy(example_udp_t *udp) { delete udp; }

wl_err_t example_udp_start(example_udp_t *udp, size_t tx_max, int argc, char **argv,
                           uint16_t default_local, uint16_t default_peer) {
  const char *local_address = "127.0.0.1";
  const char *peer_address = "127.0.0.1";
  int32_t local = default_local;
  int32_t peer = default_peer;
  if (argc == 5 && example_int32(argv[2], &local) && example_int32(argv[4], &peer) &&
      local > 0 && local <= UINT16_MAX && peer > 0 && peer <= UINT16_MAX) {
    local_address = argv[1];
    peer_address = argv[3];
  } else if (argc != 1) {
    std::fprintf(stderr, "usage: %s [LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT]\n", argv[0]);
    return WL_ERR_INVALID_ARG;
  }
  try {
    wirelink::asio::PacketUdpConfig config;
    config.bind_address = local_address;
    config.bind_port = static_cast<uint16_t>(local);
    config.maximum_tx_datagram_size = tx_max;
    std::error_code error;
    wl_err_t status = udp->adapter.start(config, error);
    if (status == WL_OK)
      status = udp->adapter.set_peer(peer_address, static_cast<uint16_t>(peer));
    if (status != WL_OK) {
      std::fprintf(stderr, "UDP open: %s\n", error ? error.message().c_str() : wl_err_str(status));
      return status;
    }
    std::signal(SIGINT, stopped);
    std::signal(SIGTERM, stopped);
    return WL_OK;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "UDP open: %s\n", error.what());
    return WL_ERR_IO;
  }
}
/* [docs:udp_host_step] */
wl_err_t example_udp_step(example_udp_t *udp, wl_endpoint_t *core, uint32_t now) {
  try {
    const auto error = udp->adapter.service();
    const auto advanced = wl_endpoint_step(core, now, 16);
    return error == WL_OK ? advanced : error;
  } catch (...) { return WL_ERR_IO; }
}
/* [/docs:udp_host_step] */
wl_err_t example_udp_wait(example_udp_t *udp, wl_endpoint_t *core, uint32_t maximum_ms) {
  const uint32_t sampled = example_now_ms();
  int has_deadline = 0; uint32_t deadline = 0;
  if (wl_endpoint_ready(core, sampled, &has_deadline, &deadline) || !example_running()) return WL_OK;
  uint32_t limit = maximum_ms;
  if (has_deadline) limit = std::min(limit, deadline - sampled);
  const uint32_t elapsed = example_now_ms() - sampled;
  if (elapsed >= limit) return WL_OK;
  try {
    const auto error = udp->adapter.wait_for_activity(std::chrono::milliseconds(limit - elapsed));
    return error == WL_ERR_NO_DATA ? WL_OK : error;
  } catch (...) { return WL_ERR_IO; }
}
wl_err_t example_udp_close(example_udp_t *udp, wl_endpoint_t *core) {
  const auto error = wl_endpoint_close(core);
  if (error != WL_OK) return error;
  udp->adapter.stop();
  const auto advanced = wl_endpoint_step(core, example_now_ms(), 16);
  if (advanced != WL_OK) return advanced;
  if (!wl_endpoint_quiescent(core)) return WL_ERR_BUSY;
  delete udp; return WL_OK;
}
