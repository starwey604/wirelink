/* SPDX-License-Identifier: Apache-2.0 */
#include "tutorial_host.h"
#include "wirelink/asio/udp_adapter.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <limits>
#include <memory>

struct example_udp {
  wl_endpoint_t *endpoint{};
  std::unique_ptr<wirelink::asio::UdpAdapter> adapter;
};

static volatile std::sig_atomic_t running = 1;
static void stop(int) { running = 0; }

wl_time_ms_t example_now_ms(void) {
  const auto environment = wl_platform_environment();
  return environment.clock.now_ms(environment.clock.user_data);
}

int example_running(void) { return running != 0; }

int example_int32(const char *text, int32_t *value) {
  if (text == nullptr || *text == '\0' || value == nullptr) return 0;
  char *end = nullptr;
  errno = 0;
  const long long parsed = std::strtoll(text, &end, 10);
  if (errno != 0 || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) return 0;
  *value = static_cast<int32_t>(parsed);
  return 1;
}

int example_ports(int argc, char **argv, uint16_t *local, uint16_t *peer) {
  if (argc == 1) return 1;
  int32_t first, second;
  if (argc != 3 || !example_int32(argv[1], &first) || !example_int32(argv[2], &second) ||
      first < 1 || first > UINT16_MAX || second < 1 || second > UINT16_MAX || first == second) {
    std::fprintf(stderr, "usage: %s [LOCAL_PORT PEER_PORT]\n", argv[0]);
    return 0;
  }
  *local = static_cast<uint16_t>(first);
  *peer = static_cast<uint16_t>(second);
  return 1;
}

example_udp_t *example_udp_open(wl_endpoint_t *endpoint, uint16_t local, uint16_t peer) {
  return example_udp_open_at(endpoint, "127.0.0.1", local, "127.0.0.1", peer);
}

example_udp_t *example_udp_open_at(wl_endpoint_t *endpoint, const char *local_address,
    uint16_t local, const char *peer_address, uint16_t peer) {
  if (endpoint == nullptr || local_address == nullptr || peer_address == nullptr || peer == 0)
    return nullptr;
  try {
    auto udp = std::make_unique<example_udp_t>();
    udp->endpoint = endpoint;
    wirelink::asio::UdpAdapterConfig config;
    config.bind_address = local_address;
    config.bind_port = local;
    std::error_code error;
    udp->adapter = wirelink::asio::UdpAdapter::open(*endpoint, config, error);
    if (!udp->adapter) {
      std::fprintf(stderr, "UDP open: %s\n", error.message().c_str());
      return nullptr;
    }
    if (udp->adapter->set_peer(peer_address, peer) != WL_OK) return nullptr;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    return udp.release();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "UDP open: %s\n", error.what());
    return nullptr;
  }
}

int example_udp_wait(example_udp_t *udp, uint32_t maximum_ms) {
  wl_poll_hint_t hint{};
  const int status = wl_endpoint_get_hint(udp->endpoint, &hint);
  if (status != WL_OK) return status;
  if (hint.work_pending || !example_running()) return WL_OK;
  const auto duration = std::min(maximum_ms, hint.next_deadline_ms);
  try {
    const int result = udp->adapter->wait_for_activity(std::chrono::milliseconds(duration));
    return result == WL_ERR_NO_DATA ? WL_OK : result;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "UDP wait: %s\n", error.what());
    return WL_ERR_IO;
  }
}

void example_udp_close(example_udp_t *udp) { delete udp; }
