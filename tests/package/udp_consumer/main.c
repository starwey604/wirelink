/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/asio/packet_udp.h>

wl_packet_port_t *installed_udp_port(void);
int installed_udp_prepare(wl_packet_sink_fn submit, wl_packet_notify_fn notify, void *user);

int main(void) {
  wl_packet_port_t *port = installed_udp_port();
  wl_asio_udp_t *udp = wl_asio_udp_create(port);
  if (!udp) return 1;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  const wl_endpoint_config_t endpoint = {.port = port};
  const wl_asio_udp_config_t config = {.bind_address = "127.0.0.1"};
  wl_endpoint_t core;
  int status = 1;
  if (installed_udp_prepare(io.submit, io.notify, io.user) == WL_OK &&
      wl_endpoint_init(&core, &endpoint) == WL_OK &&
      wl_asio_udp_start(udp, &config) == WL_OK) {
    status = wl_asio_udp_local_port(udp) != 0 &&
             wl_asio_udp_step_endpoint(udp, &core, 0) == WL_OK ? 0 : 1;
    if (wl_asio_udp_close_endpoint(udp, &core, 0) != WL_OK) status = 1;
  }
  wl_asio_udp_destroy(udp);
  return status;
}
