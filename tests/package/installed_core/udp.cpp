/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/udp.h>
#include <memory>

int main() {
  wl_udp_t *udp{};
  wl_udp_config_t config{};
  config.bind_address = "127.0.0.1";
  if (wl_udp_open(&udp, &config, nullptr) != WL_OK) return 1;
  std::unique_ptr<wl_udp_t, decltype(&wl_udp_close)> socket(udp, wl_udp_close);
  return wl_udp_local_port(socket.get()) && wl_udp_transport(socket.get()) ? 0 : 1;
}
