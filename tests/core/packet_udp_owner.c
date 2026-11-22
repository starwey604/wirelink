/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/asio/packet_udp.h"
#include <assert.h>
#include <string.h>
wl_packet_port_t *packet_udp_test_port(unsigned index) {
  static wl_packet_port_t ports[3];
  return index < 3 ? &ports[index] : NULL;
}
int packet_udp_test_init(unsigned index, wl_packet_sink_fn sink, wl_packet_notify_fn notify,
                         void *user) {
  static uint8_t bytes[3][2][256];
  static uint32_t instance = 880U;
  wl_packet_config_t c = {.instance = ++instance,
                          .rx_storage = &bytes[index][0][0],
                          .rx_unit_size = 256,
                          .rx_storage_size = sizeof(bytes[index]),
                          .submit = sink,
                          .notify = notify,
                          .user = user};
  return wl_packet_init(packet_udp_test_port(index), &c);
}

static wl_err_t receive_message(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  unsigned *received = core->user;
  assert(frame->kind == WL_WIRE_MSG && frame->message_id == 8U);
  ++*received;
  return WL_OK;
}

void packet_udp_test_c_api(void) {
  wl_endpoint_t core[2];
  wl_asio_udp_t *udp[2];
  unsigned received = 0;
  uint8_t tx[32];
  assert(wl_asio_udp_create(NULL) == NULL);
  assert(wl_asio_udp_io(NULL).submit == NULL);
  assert(wl_asio_udp_start(NULL, NULL) == WL_ERR_INVALID_ARG);
  for (unsigned side = 0; side < 2; ++side) {
    wl_packet_port_t *port = packet_udp_test_port(side);
    udp[side] = wl_asio_udp_create(port);
    assert(udp[side] != NULL);
    const wl_packet_io_t io = wl_asio_udp_io(udp[side]);
    assert(io.submit == wl_asio_udp_submit && io.notify == wl_asio_udp_notify && io.user == udp[side]);
    assert(packet_udp_test_init(side, io.submit, io.notify, io.user) == WL_OK);
    wl_endpoint_config_t endpoint = {.port = port, .route = receive_message, .user = &received};
    if (side == 0) {
      endpoint.storage.message = tx;
      endpoint.storage.message_capacity = sizeof(tx);
    }
    assert(wl_endpoint_init(&core[side], &endpoint) == WL_OK);
  }

  wl_asio_udp_config_t config = {.bind_address = "127.0.0.1"};
  assert(wl_asio_udp_start(udp[1], &config) == WL_OK);
  const uint16_t peer_port = wl_asio_udp_local_port(udp[1]);
  assert(peer_port != 0);

  config.peer_port = peer_port;
  assert(wl_asio_udp_start(udp[0], &config) == WL_ERR_INVALID_ARG);
  config.peer_address = "invalid";
  assert(wl_asio_udp_start(udp[0], &config) == WL_ERR_INVALID_ARG);
  assert(wl_asio_udp_local_port(udp[0]) == 0 && !wl_asio_udp_ready(udp[0]));
  config.peer_address = "127.0.0.1";
  assert(wl_asio_udp_start(udp[0], &config) == WL_OK);
  assert(wl_asio_udp_set_peer(udp[1], config.peer_address, wl_asio_udp_local_port(udp[0])) == WL_OK);

  assert(wl_asio_udp_step_endpoint(udp[0], NULL, 0) == WL_ERR_INVALID_ARG);
  assert(wl_asio_udp_wait_endpoint(udp[0], &core[1], 0, 0) == WL_ERR_INVALID_ARG);
  assert(wl_asio_udp_close_endpoint(udp[0], &core[1], 0) == WL_ERR_INVALID_ARG);
  uint8_t *frame;
  const uint8_t header[] = {WL_WIRE_MAGIC, WL_WIRE_MSG, 8U, 0U};
  assert(wl_endpoint_msg_claim(&core[0], &frame, NULL) == WL_OK);
  memcpy(frame, header, sizeof(header));
  assert(wl_endpoint_msg_commit(&core[0], sizeof(header)) == WL_OK);
  assert(wl_asio_udp_step_endpoint(udp[0], &core[0], 0) == WL_OK);
  for (unsigned pass = 0; !received && pass < 20; ++pass) {
    assert(wl_asio_udp_wait_endpoint(udp[1], &core[1], 0, 50) == WL_OK);
    assert(wl_asio_udp_step_endpoint(udp[1], &core[1], 0) == WL_OK);
  }
  assert(received == 1 && wl_asio_udp_stats(udp[1]).received == 1);
  assert(wl_asio_udp_stats(udp[0]).sent == 1);
  for (unsigned side = 0; side < 2; ++side) {
    assert(wl_asio_udp_close_endpoint(udp[side], &core[side], 0) == WL_OK);
    assert(wl_endpoint_quiescent(&core[side]));
    assert(wl_asio_udp_start(udp[side], &config) == WL_ERR_INVALID_STATE);
    wl_asio_udp_destroy(udp[side]);
  }
}
