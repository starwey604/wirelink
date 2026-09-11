/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <errno.h>
#include <zephyr/ztest.h>
#include "udp_demo_endpoint.h"
#include "wirelink/zephyr/udp.h"
#include "../../../../tests/support/test_environment.h"

static udp_demo_endpoint_t client, server;
WL_ZEPHYR_UDP_DEFINE(client_udp, UDP_DEMO_ENDPOINT_MAX_PAYLOAD, 2);
WL_ZEPHYR_UDP_DEFINE(server_udp, UDP_DEMO_ENDPOINT_MAX_PAYLOAD, 2);
static wl_time_ms_t now;
static bool real_clock;
static unsigned completions, handlers;
static wl_rpc_completion_t completion;
static add_response_value_t saved_response;
static struct k_thread server_thread;
K_THREAD_STACK_DEFINE(server_stack, 4096);
static bool thread_active;
static wl_err_t server_result;
static int relay_fds[2] = {-1, -1};

static wl_time_ms_t clock_read(void *context) {
  (void)context;
  return real_clock ? k_uptime_get_32() : now;
}

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  (void)context;
  ++handlers;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

static void done(void *context, const wl_rpc_completion_t *result,
                  const add_response_value_t *response) {
  (void)context;
  ++completions;
  completion = *result;
  if (response != NULL) saved_response = *response;
}

static add_request_value_t request(int32_t left, int32_t right) {
  add_request_value_t value;
  add_request_value_clear(&value);
  value.has_left = value.has_right = true;
  value.left = left;
  value.right = right;
  return value;
}

static void open_pair(uint16_t client_peer, uint16_t server_peer, uint64_t session) {
  const wl_clock_t clock = {.now_ms = clock_read};
  zassert_ok(udp_demo_endpoint_init(&client, test_environment_id(session, clock)));
  udp_demo_endpoint_config_t config;
  zassert_ok(udp_demo_endpoint_config_defaults(&config, test_environment_id(2, clock)));
  config.on_add = add;
  zassert_ok(udp_demo_endpoint_init_config(&server, &config));
  /* Ports are private to this Zephyr loopback stack, not host OS sockets. */
  const wl_zephyr_udp_config_t client_config = {.peer_address = "127.0.0.1",
    .peer_port = client_peer, .local_address = "127.0.0.1", .local_port = 49100};
  const wl_zephyr_udp_config_t server_config = {.peer_address = "127.0.0.1",
    .peer_port = server_peer, .local_address = "127.0.0.1", .local_port = 49101};
  zassert_ok(wl_zephyr_udp_open(&client_udp, udp_demo_endpoint_handle(&client), &client_config));
  zassert_ok(wl_zephyr_udp_open(&server_udp, udp_demo_endpoint_handle(&server), &server_config));
}

static void before(void *unused) {
  (void)unused;
  now = 1000;
  real_clock = false;
  completions = handlers = 0;
  open_pair(49101, 49100, 1);
}

static void after(void *unused) {
  (void)unused;
  if (thread_active) {
    zassert_ok(wl_zephyr_udp_request_stop(&server_udp));
    zassert_ok(k_thread_join(&server_thread, K_SECONDS(2)));
    thread_active = false;
  }
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_ok(udp_demo_endpoint_close(&server));
  zassert_ok(wl_zephyr_udp_close(&client_udp));
  zassert_ok(wl_zephyr_udp_close(&server_udp));
  for (size_t i = 0; i < ARRAY_SIZE(relay_fds); ++i) {
    if (relay_fds[i] >= 0) zassert_ok(zsock_close(relay_fds[i]));
    relay_fds[i] = -1;
  }
}

static void step_pair(void) {
  zassert_ok(udp_demo_endpoint_step(&server));
  zassert_ok(udp_demo_endpoint_step(&client));
  ++now;
}

ZTEST(udp_validation, test_async_rpc_with_continuous_telemetry) {
  add_request_value_t value = request(20, 22);
  /* No preparatory step and no inspect/release obligation. */
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  memset(&value, 0, sizeof(value));
  unsigned telemetry_seen = 0, accepted = 0;
  for (uint32_t i = 1; i <= 100; ++i) {
    telemetry_t sample;
    telemetry_clear(&sample);
    sample.has_sample = true;
    sample.sample = i;
    udp_demo_send_result_t sent = udp_demo_endpoint_send_telemetry(&server, &sample);
    if (sent.domain == UDP_DEMO_SEND_OK) ++accepted;
    else zassert_equal(sent.core_result, WL_ERR_BUSY);
    step_pair();
    const wl_err_t read = udp_demo_endpoint_read_telemetry(&client, &sample);
    if (read == WL_OK) ++telemetry_seen;
    else zassert_equal(read, WL_ERR_NO_DATA);
  }
  zassert_true(accepted > 0 && telemetry_seen > 0);
  zassert_equal(completions, 1);
  zassert_equal(handlers, 1);
  zassert_equal(completion.status, WL_RPC_SUCCESS);
  zassert_equal(saved_response.sum, 42);
  /* Reuse the small fixed call pool repeatedly; no application release calls. */
  for (unsigned call = 0; call < 8; ++call) {
    value = request(1, 2);
    unsigned target = completions + 1;
    zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
    for (unsigned i = 0; i < 100 && completions < target; ++i) step_pair();
    zassert_equal(completions, target);
    zassert_equal(completion.status, WL_RPC_SUCCESS);
    zassert_equal(saved_response.sum, 3);
    for (unsigned i = 0; i < 4; ++i) step_pair();
  }
}

ZTEST(udp_validation, test_rejection_cancel_and_generated_close) {
  add_request_value_t value = request(INT32_MAX, 1);
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  for (unsigned i = 0; i < 100 && completions == 0; ++i) step_pair();
  zassert_equal(completions, 1);
  zassert_equal(completion.status, WL_RPC_REJECTED);
  zassert_equal(completion.rejection, 1);
  for (unsigned i = 0; i < 4; ++i) step_pair();
  value = request(1, 2);
  wl_rpc_call_t call;
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, &call));
  zassert_ok(udp_demo_endpoint_cancel(&client, &call));
  for (unsigned i = 0; i < 10 && completions == 1; ++i) step_pair();
  zassert_equal(completions, 2);
  zassert_equal(completion.status, WL_RPC_CANCELLED);
  for (unsigned i = 0; i < 8; ++i) step_pair();
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  zassert_equal(wl_zephyr_udp_close(&client_udp), WL_ERR_BUSY);
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_equal(completions, 3);
  zassert_equal(completion.status, WL_RPC_CANCELLED);
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_equal(completions, 3);
  zassert_equal(wl_zephyr_udp_notify(&client_udp), WL_ERR_CANCELLED);
}

static void server_owner(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  for (;;) {
    server_result = udp_demo_endpoint_step(&server);
    if (server_result != WL_OK) break;
    server_result = wl_zephyr_udp_wait(&server_udp, UINT32_MAX);
    if (server_result != WL_OK && server_result != WL_ERR_NO_DATA) break;
  }
}

ZTEST(udp_validation, test_synchronous_rpc_with_socket_waiter) {
  real_clock = true;
  thread_active = true;
  server_result = WL_OK;
  k_thread_create(&server_thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack),
                   server_owner, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  add_request_value_t value = request(20, 22);
  add_response_value_t response;
  wl_rpc_completion_t result = udp_demo_endpoint_add_sync(&client, &value, &response, 500);
  zassert_equal(result.status, WL_RPC_SUCCESS);
  zassert_equal(response.sum, 42);
  wl_zephyr_udp_stats_t stats;
  zassert_ok(wl_zephyr_udp_get_stats(&client_udp, &stats));
  zassert_true(stats.wait_calls > 0);
  zassert_ok(wl_zephyr_udp_request_stop(&server_udp));
  zassert_ok(k_thread_join(&server_thread, K_SECONDS(2)));
  thread_active = false;
  zassert_equal(server_result, WL_ERR_CANCELLED);
}

ZTEST(udp_validation, test_absent_peer_times_out_without_periodic_polling) {
  real_clock = true;
  zassert_ok(udp_demo_endpoint_close(&server));
  zassert_ok(wl_zephyr_udp_close(&server_udp));
  add_request_value_t value = request(20, 22);
  add_response_value_t response;
  wl_rpc_completion_t result = udp_demo_endpoint_add_sync(&client, &value, &response, 250);
  zassert_equal(result.status, WL_RPC_TIMED_OUT);
  wl_zephyr_udp_stats_t stats;
  zassert_ok(wl_zephyr_udp_get_stats(&client_udp, &stats));
  zassert_true(stats.wait_calls > 0);
  zassert_equal(stats.common.errors, 0);
  /* 250 ms RPC deadline and 100 ms ACK timer need only a few waits, not
   * a mandatory 1 ms receive poll. This is a logical count, not a CPU gate. */
  zassert_true(stats.wait_calls <= 8);
}

/* Test-only UDP relay: faults happen BETWEEN real sockets, never by replacing
 * adapter syscalls. Each side preserves the fixed source address/port. */
static void start_relay(void) {
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_ok(udp_demo_endpoint_close(&server));
  zassert_ok(wl_zephyr_udp_close(&client_udp));
  zassert_ok(wl_zephyr_udp_close(&server_udp));
  for (size_t i = 0; i < ARRAY_SIZE(relay_fds); ++i) {
    relay_fds[i] = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    zassert_true(relay_fds[i] >= 0);
    struct net_sockaddr_in address = {.sin_family = NET_AF_INET,
      .sin_addr = NET_INADDR_LOOPBACK_INIT, .sin_port = net_htons(49102 + i)};
    zassert_ok(zsock_bind(relay_fds[i], (struct net_sockaddr *)&address, sizeof(address)));
  }
  open_pair(49102, 49103, 1);
}

typedef struct {
  unsigned drop_data, drop_ack, duplicate_data;
  unsigned data_dropped, ack_dropped, duplicated;
  uint8_t saved[128];
  size_t saved_length;
} relay_faults_t;

static void forward(unsigned side, const uint8_t *bytes, size_t length) {
  struct net_sockaddr_in address = {.sin_family = NET_AF_INET,
    .sin_addr = NET_INADDR_LOOPBACK_INIT, .sin_port = net_htons(side == 0 ? 49101 : 49100)};
  zassert_equal(zsock_sendto(relay_fds[1 - side], bytes, length, ZSOCK_MSG_DONTWAIT,
      (struct net_sockaddr *)&address, sizeof(address)), (ssize_t)length);
}

static void relay(unsigned side, relay_faults_t *faults) {
  for (unsigned i = 0; i < 8; ++i) {
    uint8_t bytes[128];
    const ssize_t length = zsock_recv(relay_fds[side], bytes, sizeof(bytes), ZSOCK_MSG_DONTWAIT);
    if (length < 0) {
      zassert_true(errno == EAGAIN || errno == EWOULDBLOCK);
      return;
    }
    wl_frame_view_t view;
    zassert_ok(wl_frame_decode(bytes, (size_t)length, WL_INTEGRITY_CRC32C, &view));
    const bool reliable_data = view.type == WL_PACKET_DATA &&
        (view.flags & WL_PACKET_FLAG_RELIABLE) != 0;
    if (reliable_data) {
      memcpy(faults->saved, bytes, (size_t)length);
      faults->saved_length = (size_t)length;
      if (faults->drop_data > 0) {
        --faults->drop_data;
        ++faults->data_dropped;
        continue;
      }
    }
    if (view.type == WL_PACKET_ACK && faults->drop_ack > 0) {
      --faults->drop_ack;
      ++faults->ack_dropped;
      continue;
    }
    forward(side, bytes, (size_t)length);
    if (reliable_data && faults->duplicate_data > 0) {
      --faults->duplicate_data;
      ++faults->duplicated;
      forward(side, bytes, (size_t)length);
    }
  }
}

ZTEST(udp_validation, test_loss_duplicate_and_ack_loss_with_telemetry) {
  start_relay();
  relay_faults_t outbound = {.drop_data = 1, .drop_ack = 1, .duplicate_data = 1};
  relay_faults_t inbound = {.drop_data = 1, .drop_ack = 1};
  add_request_value_t value = request(20, 22);
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 2000, done, NULL, NULL));
  unsigned seen = 0, maximum_age = 0;
  for (uint32_t tick = 1; tick <= 300; ++tick) {
    telemetry_t sample;
    telemetry_clear(&sample);
    sample.has_sample = true;
    sample.sample = tick;
    udp_demo_send_result_t sent = udp_demo_endpoint_send_telemetry(&server, &sample);
    zassert_true(sent.domain == UDP_DEMO_SEND_OK || sent.core_result == WL_ERR_BUSY);
    relay(0, &outbound);
    zassert_ok(udp_demo_endpoint_step(&server));
    relay(1, &inbound);
    zassert_ok(udp_demo_endpoint_step(&client));
    wl_err_t read = udp_demo_endpoint_read_telemetry(&client, &sample);
    if (read == WL_OK) {
      ++seen;
      zassert_true(sample.sample <= tick);
      const unsigned age = tick - sample.sample;
      if (age > maximum_age) maximum_age = age;
    } else zassert_equal(read, WL_ERR_NO_DATA);
    now += 10;
  }
  zassert_equal(completions, 1);
  zassert_equal(handlers, 1);
  zassert_equal(completion.status, WL_RPC_SUCCESS);
  zassert_equal(saved_response.sum, 42);
  zassert_equal(outbound.data_dropped, 1);
  zassert_equal(inbound.data_dropped, 1);
  zassert_equal(outbound.ack_dropped, 1);
  zassert_equal(inbound.ack_dropped, 1);
  zassert_equal(outbound.duplicated, 1);
  zassert_true(seen >= 290);
  zassert_true(maximum_age <= 2);
  /* Logical scheduling/ownership gate only; no simulated CPU/latency claim. */
  TC_PRINT("UDP_MIXED samples=%u max_age_steps=%u handlers=%u\n", seen, maximum_age, handlers);
}

ZTEST(udp_validation, test_blackhole_timeout_then_reuse_call_pool) {
  start_relay();
  relay_faults_t outbound = {.drop_data = UINT_MAX};
  relay_faults_t inbound = {0};
  add_request_value_t value = request(4, 5);
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 250, done, NULL, NULL));
  for (unsigned i = 0; i < 30; ++i) {
    relay(0, &outbound);
    relay(1, &inbound);
    step_pair();
    now += 9;
  }
  zassert_equal(completions, 1);
  zassert_equal(completion.status, WL_RPC_TIMED_OUT);
  zassert_equal(handlers, 0);
  /* Same endpoint/socket, no release and no reinitialization after timeout. */
  outbound.drop_data = 0;
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  for (unsigned i = 0; i < 60; ++i) {
    relay(0, &outbound);
    relay(1, &inbound);
    step_pair();
    now += 9;
  }
  zassert_equal(completions, 2);
  zassert_equal(completion.status, WL_RPC_SUCCESS, "status=%d local=%d handlers=%u",
                 completion.status, completion.local_error, handlers);
  zassert_equal(saved_response.sum, 9);
  zassert_equal(handlers, 1);
}

ZTEST(udp_validation, test_rebuild_same_endpoint_rejects_old_response) {
  start_relay();
  relay_faults_t outbound = {0}, inbound = {0};
  add_request_value_t value = request(20, 22);
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  for (unsigned i = 0; i < 40; ++i) {
    relay(0, &outbound);
    relay(1, &inbound);
    step_pair();
  }
  zassert_equal(completions, 1);
  zassert_equal(completion.status, WL_RPC_SUCCESS);
  zassert_true(inbound.saved_length > 0);
  uint8_t old_response[128];
  size_t old_length = inbound.saved_length;
  memcpy(old_response, inbound.saved, old_length);
  wl_rpc_call_t old_call;
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, &old_call));
  zassert_ok(wl_zephyr_udp_request_stop(&client_udp));
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_equal(completions, 2);
  zassert_equal(completion.status, WL_RPC_CANCELLED);
  zassert_ok(wl_zephyr_udp_close(&client_udp));
  /* Model coordinated cutover: discard old queued outbound requests at relay.
   * A new session is not general anti-replay protection for stale requests. */
  outbound.drop_data = UINT_MAX;
  relay(0, &outbound);
  outbound.drop_data = 0;
  const wl_clock_t clock = {.now_ms = clock_read};
  zassert_ok(udp_demo_endpoint_init(&client, test_environment_id(3, clock)));
  const wl_zephyr_udp_config_t network = {.peer_address = "127.0.0.1",
    .peer_port = 49102, .local_address = "127.0.0.1", .local_port = 49100};
  zassert_ok(wl_zephyr_udp_open(&client_udp, udp_demo_endpoint_handle(&client), &network));
  zassert_equal(udp_demo_endpoint_cancel(&client, &old_call), WL_ERR_NOT_FOUND);
  value = request(100, 2);
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  forward(1, old_response, old_length);
  zassert_ok(udp_demo_endpoint_step(&client));
  zassert_equal(completions, 2); /* Old response cannot satisfy the new call. */
  for (unsigned i = 0; i < 80; ++i) {
    relay(0, &outbound);
    relay(1, &inbound);
    step_pair();
  }
  zassert_equal(completions, 3);
  zassert_equal(completion.status, WL_RPC_SUCCESS);
  zassert_equal(saved_response.sum, 102);
  zassert_equal(handlers, 2);
}

ZTEST_SUITE(udp_validation, NULL, NULL, before, after, NULL);
