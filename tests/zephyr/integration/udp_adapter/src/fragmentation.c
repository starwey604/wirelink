/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include "wirelink/zephyr/udp.h"

WL_ZEPHYR_UDP_DEFINE(udp, 64, 2);
static wl_endpoint_t endpoint;
static uint8_t payload[64], tx[128], control[32], rx[128], fifo[256];

static wl_time_ms_t read_clock(void *context) {
  (void)context;
  return 0;
}

ZTEST(udp_fragmentation, test_no_fragmentation_or_reject_before_attachment) {
  const wl_config_t link = {.max_payload_len = sizeof(payload),
    .envelope = WL_ENVELOPE_NATIVE_PACKET, .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 1, .ack_timeout_ms = 100, .max_retries = 2};
  const wl_storage_t storage = {.tx_payload = payload, .tx_payload_size = sizeof(payload),
    .tx_unit = tx, .tx_unit_size = sizeof(tx), .control_unit = control,
    .control_unit_size = sizeof(control), .rx_fallback = rx, .rx_fallback_size = sizeof(rx),
    .rx_fifo = fifo, .rx_fifo_size = sizeof(fifo)};
  const wl_clock_t clock = {.now_ms = read_clock};
  const wl_pump_hooks_t hooks = {0};
  const wl_zephyr_udp_config_t config = {.peer_address = "127.0.0.1",
    .peer_port = 49101, .local_address = "127.0.0.1"};
  zassert_ok(wl_endpoint_init(&endpoint, &link, &storage, &clock, &hooks));
#ifdef ZSOCK_IP_DONTFRAG
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoint, &config));
  int disabled = 0;
  net_socklen_t size = sizeof(disabled);
  zassert_ok(zsock_getsockopt(udp.private_state.io.socket_fd, NET_IPPROTO_IP,
      ZSOCK_IP_DONTFRAG, &disabled, &size));
  zassert_equal(disabled, 1);
#else
  for (unsigned attempt = 0; attempt < 20; ++attempt) {
    zassert_equal(wl_zephyr_udp_open(&udp, &endpoint, &config), WL_ERR_NOT_SUPPORTED);
    zassert_false(udp.private_state.io.opened);
    zassert_false(wl_endpoint_has_adapter(&endpoint));
    zassert_is_null(wl_endpoint_waiter(&endpoint));
  }
#endif
  zassert_ok(wl_zephyr_udp_close(&udp));
  wl_endpoint_close(&endpoint);
}

ZTEST_SUITE(udp_fragmentation, NULL, NULL, NULL, NULL, NULL);
