/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/asio/packet_udp.hpp>
extern "C" wl_packet_port_t *installed_udp_port(void);
extern "C" int installed_udp_prepare(wl_packet_sink_fn, wl_packet_notify_fn, void *);
int main() {
  using namespace std::chrono_literals;
  using wirelink::asio::PacketUdpAdapter;
  auto *port = installed_udp_port(); PacketUdpAdapter adapter(*port);
  if (installed_udp_prepare(PacketUdpAdapter::submit, PacketUdpAdapter::notify, &adapter) != WL_OK) return 1;
  wirelink::asio::PacketUdpConfig config; config.bind_address = "127.0.0.1";
  config.maximum_datagram_size = 200; std::error_code error;
  if (adapter.start(config, error) != WL_OK || error) return 2;
  if (adapter.service() != WL_OK) return 3;
  for (unsigned i = 0; i < 4; ++i) (void)adapter.wait_for_activity(0ms);
  if (adapter.wait_for_activity(2ms) != WL_ERR_NO_DATA) return 4;
  PacketUdpAdapter::notify(&adapter);
  if (adapter.wait_for_activity(10ms) != WL_OK) return 5;
  adapter.stop(); return wl_packet_quiescent(port) ? 0 : 6;
}
