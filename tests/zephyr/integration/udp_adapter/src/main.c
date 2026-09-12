/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/ztest.h>
#include "wirelink/zephyr/udp.h"

#define PAYLOAD_BOUND 64U
WL_ZEPHYR_UDP_DEFINE(udp, PAYLOAD_BOUND, 2);
WL_ZEPHYR_UDP_DEFINE(other_udp, PAYLOAD_BOUND, 2);
WL_ZEPHYR_UDP_DEFINE(small_udp, 1, 2);
static wl_endpoint_t endpoints[2];
static struct {
  uint8_t payload[PAYLOAD_BOUND], tx[128], control[32], rx[128], fifo[256];
} storage[2];
static wl_time_ms_t now[2];
static unsigned clock_reads[2], received[2];
static uint8_t last_value[2];
static wl_zephyr_udp_config_t config;
static int peer_fd;
static struct net_pkt *held[CONFIG_NET_PKT_TX_COUNT];
static size_t held_count;

static wl_time_ms_t read_clock(void *context) {
  size_t index = (size_t)context;
  ++clock_reads[index];
  return now[index];
}

static wl_pump_event_disposition_t on_event(void *context, wl_ctx_t *link,
    const wl_event_t *event, wl_time_ms_t timestamp) {
  size_t index = (size_t)context;
  (void)link;
  zassert_equal(timestamp, now[index]);
  if (event->type == WL_EVT_UNRELIABLE_RX || event->type == WL_EVT_RELIABLE_RX) {
    ++received[index];
    zassert_equal(event->payload_len, 1);
    last_value[index] = event->payload[0];
  }
  return WL_PUMP_EVENT_UNHANDLED;
}

static void init_endpoint(size_t index, wl_envelope_type_t envelope) {
  const wl_config_t link_config = {.max_payload_len = PAYLOAD_BOUND,
    .envelope = envelope, .integrity = WL_INTEGRITY_CRC32C,
    .session_id = index + 1U, .ack_timeout_ms = 100, .max_retries = 2};
  const wl_storage_t memory = {.tx_payload = storage[index].payload,
    .tx_payload_size = sizeof(storage[index].payload),
    .tx_unit = storage[index].tx, .tx_unit_size = sizeof(storage[index].tx),
    .control_unit = storage[index].control, .control_unit_size = sizeof(storage[index].control),
    .rx_fallback = storage[index].rx, .rx_fallback_size = sizeof(storage[index].rx),
    .rx_fifo = storage[index].fifo, .rx_fifo_size = sizeof(storage[index].fifo)};
  const wl_clock_t clock = {.now_ms = read_clock, .user_data = (void *)index};
  const wl_pump_hooks_t application = {.on_event = on_event, .application_user_data = (void *)index};
  zassert_ok(wl_endpoint_init(&endpoints[index], &link_config, &memory, &clock, &application));
}

static void release_packets(void) {
  for (size_t i = 0; i < held_count; ++i) net_pkt_unref(held[i]);
  held_count = 0;
}

static void exhaust_packets(void) {
  while (held_count < ARRAY_SIZE(held)) {
    struct net_pkt *packet = net_pkt_alloc(K_NO_WAIT);
    if (packet == NULL) break;
    held[held_count++] = packet;
  }
  zassert_equal(held_count, ARRAY_SIZE(held));
}

static void before(void *unused) {
  (void)unused;
  memset(clock_reads, 0, sizeof(clock_reads));
  memset(received, 0, sizeof(received));
  now[0] = 1000;
  now[1] = 7000;
  peer_fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
  zassert_true(peer_fd >= 0);
  struct net_sockaddr_in address = {.sin_family = NET_AF_INET,
                                    .sin_addr = NET_INADDR_LOOPBACK_INIT};
  zassert_ok(zsock_bind(peer_fd, (struct net_sockaddr *)&address, sizeof(address)));
  net_socklen_t length = sizeof(address);
  zassert_ok(zsock_getsockname(peer_fd, (struct net_sockaddr *)&address, &length));
  config = (wl_zephyr_udp_config_t){.peer_address = "127.0.0.1",
    .peer_port = net_ntohs(address.sin_port), .local_address = "127.0.0.1", .tx_retry_ms = 10};
  init_endpoint(0, WL_ENVELOPE_NATIVE_PACKET);
  init_endpoint(1, WL_ENVELOPE_NATIVE_PACKET);
}

static void after(void *unused) {
  (void)unused;
  release_packets();
  wl_endpoint_close(&endpoints[0]);
  wl_endpoint_close(&endpoints[1]);
  zassert_ok(wl_zephyr_udp_close(&udp));
  zassert_ok(wl_zephyr_udp_close(&other_udp));
  zassert_ok(wl_zephyr_udp_close(&small_udp));
  zassert_ok(zsock_close(peer_fd));
}

static wl_zephyr_udp_stats_t stats(wl_zephyr_udp_t *adapter) {
  wl_zephyr_udp_stats_t result;
  zassert_ok(wl_zephyr_udp_get_stats(adapter, &result));
  return result;
}

static void send_bytes(const uint8_t *bytes, size_t length) {
  struct net_sockaddr_in destination = {.sin_family = NET_AF_INET,
    .sin_addr = NET_INADDR_LOOPBACK_INIT, .sin_port = net_htons(wl_zephyr_udp_local_port(&udp))};
  zassert_equal(zsock_sendto(peer_fd, bytes, length, ZSOCK_MSG_DONTWAIT,
      (struct net_sockaddr *)&destination, sizeof(destination)), (ssize_t)length);
}

static void send_value(uint8_t value) {
  uint8_t frame[128];
  size_t length;
  const wl_wire_packet_t packet = {.type = WL_PACKET_DATA,
    .integrity = WL_INTEGRITY_CRC32C, .message_id = 42, .payload = &value, .payload_len = 1};
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, frame, sizeof(frame), &length));
  send_bytes(frame, length);
}

static size_t read_peer(uint8_t *bytes, size_t capacity) {
  struct zsock_pollfd fd = {.fd = peer_fd, .events = ZSOCK_POLLIN};
  zassert_equal(zsock_poll(&fd, 1, 1000), 1);
  ssize_t length = zsock_recv(peer_fd, bytes, capacity, ZSOCK_MSG_DONTWAIT);
  zassert_true(length > 0);
  return (size_t)length;
}

ZTEST(udp_adapter, test_automatic_attach_and_callback_free_wait) {
  extern int udp_cpp_headers(void);
  zassert_true(udp_cpp_headers());
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  zassert_true(wl_endpoint_has_adapter(&endpoints[0]));
  zassert_not_null(wl_endpoint_waiter(&endpoints[0]));
  zassert_not_equal(wl_zephyr_udp_local_port(&udp), 0);
#ifdef ZSOCK_IP_DONTFRAG
  int dont_fragment = 0;
  net_socklen_t option_size = sizeof(dont_fragment);
  zassert_ok(zsock_getsockopt(udp.private_state.io.socket_fd, NET_IPPROTO_IP,
      ZSOCK_IP_DONTFRAG, &dont_fragment, &option_size));
  zassert_equal(dont_fragment, 1);
#else
  zassert_false(IS_ENABLED(CONFIG_NET_IPV4_FRAGMENT));
#endif
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, UINT32_MAX);
  send_value(42);
  zassert_ok(wl_zephyr_udp_wait(&udp, 1000));
  zassert_equal(received[0], 0);
  zassert_equal(stats(&udp).common.service_calls, 0);
  unsigned before_reads = clock_reads[0];
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(clock_reads[0], before_reads + 1);
  zassert_equal(received[0], 1);
  zassert_equal(last_value[0], 42);
  zassert_equal(stats(&udp).common.rx_units, 1);
  zassert_equal(wl_zephyr_udp_wait(&udp, 0), WL_ERR_NO_DATA);
}

ZTEST(udp_adapter, test_config_failure_does_not_attach) {
  wl_zephyr_udp_config_t invalid = config;
  invalid.peer_port = 0;
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &invalid), WL_ERR_INVALID_ARG);
  invalid = config;
  invalid.peer_address = "localhost";
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &invalid), WL_ERR_INVALID_ARG);
  invalid.peer_address = "::1";
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &invalid), WL_ERR_INVALID_ARG);
  invalid = config;
  invalid.maximum_datagram_size = 16;
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &invalid), WL_ERR_FRAME_TOO_LONG);
  invalid = config;
  invalid.tx_retry_ms = UINT32_MAX;
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &invalid), WL_ERR_INVALID_ARG);
  zassert_equal(wl_zephyr_udp_open(&small_udp, &endpoints[0], &config), WL_ERR_BUF_TOO_SMALL);
  invalid = config;
  invalid.local_port = config.peer_port;
  for (unsigned i = 0; i < 20; ++i)
    zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &invalid), WL_ERR_IO);
  zassert_false(wl_endpoint_has_adapter(&endpoints[0]));
  zassert_is_null(wl_endpoint_waiter(&endpoints[0]));
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &config), WL_ERR_INVALID_STATE);
  zassert_equal(wl_zephyr_udp_open(&other_udp, &endpoints[0], &config), WL_ERR_BUSY);
}

static wl_err_t dummy_wait(void *context, uint32_t maximum) {
  (void)context; (void)maximum;
  return WL_ERR_NO_DATA;
}

ZTEST(udp_adapter, test_reject_cobs_and_preserve_existing_waiter) {
  wl_endpoint_close(&endpoints[0]);
  init_endpoint(0, WL_ENVELOPE_COBS_STREAM);
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &config), WL_ERR_NOT_SUPPORTED);
  wl_endpoint_close(&endpoints[0]);
  init_endpoint(0, WL_ENVELOPE_NATIVE_PACKET);
  const wl_waiter_t waiter = {.wait = dummy_wait};
  zassert_ok(wl_endpoint_set_waiter(&endpoints[0], &waiter));
  zassert_equal(wl_zephyr_udp_open(&udp, &endpoints[0], &config), WL_ERR_BUSY);
  zassert_equal(wl_endpoint_waiter(&endpoints[0])->wait, dummy_wait);
  zassert_false(wl_endpoint_has_adapter(&endpoints[0]));
}

ZTEST(udp_adapter, test_rejected_packets_consume_service_budget) {
  config.service_budget = 2;
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  uint8_t unused = 0;
  for (unsigned i = 0; i < 3; ++i) send_bytes(&unused, 0);
  send_value(19);
  zassert_ok(wl_zephyr_udp_wait(&udp, 1000));
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).rx_rejected, 2);
  zassert_equal(received[0], 0);
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, 0);
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).rx_rejected, 3);
  zassert_equal(received[0], 1);
  zassert_equal(last_value[0], 19);
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, UINT32_MAX);
  zassert_equal(stats(&udp).common.errors, 0);
}

ZTEST(udp_adapter, test_full_queue_drain_rearms_socket_wait) {
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  for (uint8_t i = 0; i < 3; ++i) send_value(i);
  zassert_ok(wl_zephyr_udp_wait(&udp, 1000));
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(received[0], 2);
  zassert_equal(stats(&udp).common.rx_backpressure, 1);
  zassert_true(stats(&udp).common.rx_paused);
  /* The step drained both slots after service saw full. Do not sleep with RX
   * masked now that the only remaining work is a datagram in the socket. */
  zassert_ok(wl_zephyr_udp_wait(&udp, 1000));
  zassert_false(stats(&udp).common.rx_paused);
  zassert_equal(received[0], 2);
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(received[0], 3);
  zassert_equal(last_value[0], 2);
}

ZTEST(udp_adapter, test_backpressure_deadline_gates_actual_syscalls) {
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  exhaust_packets();
  uint8_t value = 9;
  const int64_t started = k_uptime_get();
  zassert_ok(wl_send_unreliable(wl_endpoint_link(&endpoints[0]), 42, &value, 1));
  /* Diagnostic, not a benchmark or a lower-bound assertion that would freeze
   * an upstream bug: this Zephyr revision waits PKT_WAIT_TIME even with
   * MSG_DONTWAIT. Keep it visible when the stack implementation changes. */
  TC_PRINT("UDP_TX_POOL_WAIT elapsed_ms=%lld (socket allocation path)\n",
           (long long)(k_uptime_get() - started));
  zassert_equal(stats(&udp).tx_backpressure, 1);
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, 10);
  for (unsigned i = 0; i < 5; ++i) {
    unsigned before_reads = clock_reads[0];
    zassert_ok(wl_endpoint_step(&endpoints[0], 8));
    zassert_equal(clock_reads[0], before_reads + 1);
  }
  zassert_equal(stats(&udp).tx_backpressure, 1);
  zassert_true(stats(&udp).tx_deferred >= 5);
  now[0] += 10;
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).tx_backpressure, 2);
  release_packets();
  now[0] += 9;
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).common.tx_units, 0);
  ++now[0];
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).common.tx_units, 1);
  uint8_t bytes[128];
  wl_frame_view_t view;
  size_t length = read_peer(bytes, sizeof(bytes));
  zassert_ok(wl_frame_decode(bytes, length, WL_INTEGRITY_CRC32C, &view));
  zassert_equal(view.payload.data[0], value);
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, UINT32_MAX);
  zassert_equal(stats(&udp).common.errors, 0);
#ifdef CONFIG_WIRELINK_ZEPHYR_UDP_TIMING
  const wl_zephyr_udp_stats_t measured = stats(&udp);
  zassert_equal(measured.send_timing.calls, 3); /* Two BUSY, one accepted. */
  zassert_equal(measured.service_timing.calls, measured.common.service_calls);
  zassert_equal(measured.receive_timing.calls, measured.rx_idle_passes);
  zassert_true(measured.send_timing.cycles >= measured.send_timing.max_cycles);
#endif
}

ZTEST(udp_adapter, test_retry_counter_wrap_and_endpoint_isolation) {
  now[0] = UINT32_MAX - 4U;
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  zassert_ok(wl_zephyr_udp_open(&other_udp, &endpoints[1], &config));
  exhaust_packets();
  uint8_t value = 5;
  zassert_ok(wl_send_unreliable(wl_endpoint_link(&endpoints[0]), 42, &value, 1));
  release_packets();
  unsigned before_reads = clock_reads[1];
  zassert_ok(wl_send_unreliable(wl_endpoint_link(&endpoints[1]), 42, &value, 1));
  zassert_equal(clock_reads[1], before_reads);
  zassert_equal(stats(&other_udp).tx_backpressure, 0);
  now[0] = 4;
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, 1);
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).common.tx_units, 0);
  now[0] = 5;
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).common.tx_units, 1);
}

ZTEST(udp_adapter, test_cancel_clears_stale_retry_deadline) {
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  exhaust_packets();
  uint8_t value = 5;
  wl_tx_handle_t handle;
  wl_ctx_t *link = wl_endpoint_link(&endpoints[0]);
  zassert_ok(wl_send_reliable(link, 42, &value, 1, now[0], &handle));
  zassert_ok(wl_tx_cancel(link, handle));
  wl_tx_result_t result;
  zassert_ok(wl_tx_take(link, handle, &result));
  release_packets();
  now[0] += 10;
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, UINT32_MAX);
  zassert_equal(stats(&udp).common.tx_units, 0);
}

ZTEST(udp_adapter, test_ack_backpressure_uses_retry_deadline) {
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  uint8_t value = 5, frame[128];
  size_t length;
  const wl_wire_packet_t packet = {.type = WL_PACKET_DATA,
    .flags = WL_PACKET_FLAG_RELIABLE, .session_id = 9, .sequence = 7,
    .integrity = WL_INTEGRITY_CRC32C, .message_id = 42, .payload = &value, .payload_len = 1};
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, frame, sizeof(frame), &length));
  /* Inject an already-received unit, then exhaust ALL packet objects. Receiving
   * through loopback here would free its TX packet and make ACK allocation
   * succeed, accidentally avoiding the pressure path under test. */
  wl_ctx_t *link = wl_endpoint_link(&endpoints[0]);
  wl_rx_unit_claim_t claim;
  zassert_ok(wl_rx_unit_claim(link, length, &claim));
  memcpy(claim.span.data, frame, length);
  zassert_ok(wl_rx_unit_commit(link, &claim, length));
  exhaust_packets();
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(received[0], 1);
  zassert_equal(stats(&udp).tx_backpressure, 1);
  release_packets();
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, 10);
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  zassert_equal(stats(&udp).common.tx_units, 0);
  now[0] += 10;
  zassert_ok(wl_endpoint_step(&endpoints[0], 8));
  wl_frame_view_t view;
  length = read_peer(frame, sizeof(frame));
  zassert_ok(wl_frame_decode(frame, length, WL_INTEGRITY_CRC32C, &view));
  zassert_equal(view.type, WL_PACKET_ACK);
  zassert_equal(view.sequence, packet.sequence);
  zassert_equal(view.session_id, packet.session_id);
  zassert_equal(stats(&udp).common.tx_units, 1);
  zassert_ok(wl_endpoint_get_hint(&endpoints[0], &hint));
  zassert_equal(hint.next_deadline_ms, UINT32_MAX);
}

ZTEST(udp_adapter, test_waiter_notifications_and_close_order) {
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  const wl_waiter_t waiter = *wl_endpoint_waiter(&endpoints[0]);
  waiter.notify(waiter.user_data);
  zassert_ok(waiter.wait(waiter.user_data, UINT32_MAX));
  zassert_equal(stats(&udp).common.activity_notifications, 1);
  zassert_equal(wl_zephyr_udp_close(&udp), WL_ERR_BUSY);
  zassert_ok(wl_zephyr_udp_request_stop(&udp));
  zassert_equal(wl_zephyr_udp_wait(&udp, UINT32_MAX), WL_ERR_CANCELLED);
  wl_endpoint_close(&endpoints[0]);
  zassert_equal(wl_zephyr_udp_notify(&udp), WL_ERR_CANCELLED);
  zassert_equal(waiter.wait(waiter.user_data, 0), WL_ERR_CANCELLED);
  zassert_false(stats(&udp).common.started);
  zassert_ok(wl_zephyr_udp_close(&udp));
  zassert_ok(wl_zephyr_udp_close(&udp));
  zassert_equal(wl_zephyr_udp_notify(&udp), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_zephyr_udp_local_port(&udp), 0);
}

ZTEST(udp_adapter, test_old_adapter_close_cannot_close_reused_endpoint) {
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  wl_endpoint_close(&endpoints[0]);
  init_endpoint(0, WL_ENVELOPE_NATIVE_PACKET);
  zassert_ok(wl_zephyr_udp_open(&other_udp, &endpoints[0], &config));
  zassert_ok(wl_zephyr_udp_close(&udp));
  zassert_not_null(wl_endpoint_link(&endpoints[0]));
  zassert_ok(wl_zephyr_udp_notify(&other_udp));
  zassert_ok(wl_zephyr_udp_wait(&other_udp, 0));
  wl_endpoint_close(&endpoints[0]);
  zassert_ok(wl_zephyr_udp_close(&other_udp));
  init_endpoint(0, WL_ENVELOPE_NATIVE_PACKET);
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  zassert_equal(wl_zephyr_udp_wait(&udp, 0), WL_ERR_NO_DATA);
  zassert_equal(stats(&udp).common.activity_notifications, 0);
}

ZTEST(udp_adapter, test_fatal_receive_error_is_latched) {
  zassert_ok(wl_zephyr_udp_open(&udp, &endpoints[0], &config));
  /* Fault injection only: applications must never access/close private fds. */
  zassert_ok(zsock_close(udp.private_state.io.socket_fd));
  zassert_equal(wl_endpoint_step(&endpoints[0], 8), WL_ERR_IO);
  zassert_equal(wl_endpoint_step(&endpoints[0], 8), WL_ERR_IO);
  zassert_equal(wl_zephyr_udp_wait(&udp, 0), WL_ERR_IO);
  zassert_equal(stats(&udp).common.errors, 1);
  zassert_equal(stats(&udp).last_error, WL_ERR_IO);
  wl_endpoint_close(&endpoints[0]);
  zassert_equal(wl_zephyr_udp_close(&udp), WL_ERR_IO);
}

ZTEST_SUITE(udp_adapter, NULL, NULL, before, after, NULL);
