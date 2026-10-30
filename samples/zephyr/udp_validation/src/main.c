/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/net/socket.h>
#include "business.h"
#include "zephyr_owner.h"
#include <string.h>
/* Real generated RPCs over two real Zephyr UDP sockets. */
static udp_demo_kernel_t endpoints[2];
static wl_packet_port_t ports[2];
static wl_kernel_udp_t adapters[2];
static uint8_t rx[2][2][2053];
static peer_client_t states[2];
static uint32_t tick;
static uint64_t incarnation;
static unsigned executed;
static uint32_t clock_read(void *user) { (void)user; return tick; }
static struct net_sockaddr_in address(uint16_t port) {
  struct net_sockaddr_in value = {.sin_family = NET_AF_INET, .sin_port = net_htons(port),
    .sin_addr = NET_INADDR_LOOPBACK_INIT};
  return value;
}
static int32_t add(void *user, const add_request_t *request, add_response_value_t *response) {
  ++executed; return peer_add(user, request, response);
}
static void setup(void) {
  memset(states, 0, sizeof(states)); executed = 0; tick = 100;
  const uint16_t base = 49200;
  for (unsigned side = 0; side < 2; ++side) {
    memset(&adapters[side], 0, sizeof(adapters[side]));
    wl_packet_config_t pc = {.instance = ++incarnation, .rx_storage = &rx[side][0][0],
      .rx_storage_size = sizeof(rx[side]), .rx_unit_size = sizeof(rx[side][0]),
      .submit = wl_kernel_udp_submit, .notify = wl_kernel_udp_notify, .user = &adapters[side]};
    zassert_ok(wl_packet_init(&ports[side], &pc));
    wl_kernel_udp_config_t config = {.port = &ports[side], .local = address(base + side),
      .peer = address(base + 1U - side), .maximum_datagram_size = 2052, .service_budget = 8,
      .clock = {clock_read, NULL}};
    zassert_ok(wl_kernel_udp_open(&adapters[side], &config));
    const udp_demo_kernel_handlers_t handlers = {.on_rpc_add = add, .on_message_telemetry = peer_sample};
    zassert_ok(udp_demo_kernel_init(&endpoints[side], &ports[side], &handlers, NULL, &states[side]));
  }
}
static void pass(void) {
  for (unsigned side = 0; side < 2; ++side) zassert_ok(peer_z_step(&adapters[side], &endpoints[side].core, tick));
  ++tick; k_sleep(K_MSEC(1)); /* Give the real network RX thread CPU. */
}
static void close_pair(void) {
  for (unsigned side = 0; side < 2; ++side) {
    zassert_ok(peer_z_close(&adapters[side], &endpoints[side].core));
    zassert_true(wl_kernel_quiescent(&endpoints[side].core));
  }
}
static add_request_value_t request(int32_t left, int32_t right) {
  return (add_request_value_t){.has_left = true, .left = left, .has_right = true, .right = right};
}
ZTEST(udp_validation, test_four_calls_each_direction_and_ordinary_telemetry) {
  setup();
  for (unsigned side = 0; side < 2; ++side) {
    for (unsigned slot = 0; slot < 4; ++slot) {
      add_request_value_t value = request(slot, 42 - slot); wl_kernel_call_handle_t handle;
      zassert_ok(udp_demo_kernel_add_start(&endpoints[side], &value, tick, 500,
        peer_completed, &states[side], &handle));
      memset(&value, 0, sizeof(value));
    }
    add_request_value_t extra = request(1, 2); wl_kernel_call_handle_t handle;
    zassert_equal(udp_demo_kernel_add_start(&endpoints[side], &extra, tick, 500,
      peer_completed, &states[side], &handle), WL_ERR_WOULD_BLOCK);
    telemetry_value_t sample = {.has_sample = true, .sample = 1234};
    zassert_ok(udp_demo_kernel_send_telemetry(&endpoints[side], &sample));
    memset(&sample, 0, sizeof(sample));
  }
  for (unsigned i = 0; i < 500 && (states[0].completions != 4 || states[1].completions != 4 ||
      !states[0].samples || !states[1].samples); ++i) pass();
  for (unsigned side = 0; side < 2; ++side) {
    zassert_equal(states[side].completions, 4); zassert_equal(states[side].result.outcome, WL_KERNEL_SUCCESS);
    zassert_equal(states[side].response.sum, 42); zassert_equal(states[side].sample, 1234);
  }
  zassert_equal(executed, 8);
  close_pair();
  zassert_equal(states[0].response.sum, 42); /* Owned copy survives close. */
}
ZTEST(udp_validation, test_business_rejection_cancel_timeout_wrap_and_close) {
  setup();
  add_request_value_t value = request(INT32_MAX, 1); wl_kernel_call_handle_t handle;
  zassert_ok(udp_demo_kernel_add_start(&endpoints[0], &value, tick, 500, peer_completed, &states[0], &handle));
  for (unsigned i = 0; i < 500 && !states[0].completions; ++i) pass();
  zassert_equal(states[0].result.outcome, WL_KERNEL_REJECTED); zassert_equal(states[0].result.status, 1);
  value = request(20, 22); tick = UINT32_MAX - 2;
  wl_kernel_call_handle_t timed, cancelled;
  zassert_ok(udp_demo_kernel_add_start(&endpoints[0], &value, tick, 5, peer_completed, &states[0], &timed));
  zassert_ok(udp_demo_kernel_add_start(&endpoints[0], &value, tick, 500, peer_completed, &states[0], &cancelled));
  zassert_ok(wl_kernel_cancel(&endpoints[0].core, cancelled));
  zassert_ok(peer_z_step(&adapters[0], &endpoints[0].core, tick));
  zassert_equal(states[0].completions, 2); zassert_equal(states[0].result.outcome, WL_KERNEL_CANCELLED);
  tick += 5;
  zassert_ok(peer_z_step(&adapters[0], &endpoints[0].core, tick));
  zassert_equal(states[0].completions, 3); zassert_equal(states[0].result.outcome, WL_KERNEL_TIMED_OUT);
  /* The timed-out request may still execute remotely; its late response cannot
   * create a second local completion. The kernel does not roll back side effects. */
  for (unsigned i = 0; i < 20; ++i) pass();
  zassert_true(executed >= 2); zassert_equal(states[0].completions, 3);
  zassert_ok(udp_demo_kernel_add_start(&endpoints[0], &value, tick, 500, peer_completed, &states[0], &handle));
  close_pair();
  zassert_equal(states[0].completions, 4); zassert_equal(states[0].result.outcome, WL_KERNEL_CLOSED);
  setup();
  zassert_not_equal(wl_kernel_cancel(&endpoints[0].core, handle), WL_OK);
  close_pair();
}
ZTEST(udp_validation, test_finite_idle_wait_then_progress_after_notification) {
  setup();
  /* Drain initialization's latched publications. */
  while (wl_kernel_udp_wait(&adapters[0], 0) == WL_OK) { }
  uint32_t began = k_uptime_get_32();
  zassert_ok(peer_z_wait(&adapters[0], &endpoints[0].core, 15));
  zassert_true(k_uptime_get_32() - began >= 10);
  wl_kernel_udp_notify(&adapters[0]);
  began = k_uptime_get_32();
  zassert_ok(peer_z_wait(&adapters[0], &endpoints[0].core, 500));
  zassert_true(k_uptime_get_32() - began < 250);
  close_pair();
}
ZTEST_SUITE(udp_validation, NULL, NULL, NULL, NULL, NULL);
