/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/irq_offload.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/ztest.h>
#include <zephyr/zvfs/eventfd.h>

#include "socket_contract.h"
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"

#define FRAME_BOUND 128U
#define NOTIFY_ROUNDS 200U
#define PRODUCER_COUNT 4U

static wl_udp_socket_t io = WL_UDP_SOCKET_INITIALIZER;
static int sender;
static struct net_sockaddr_in peer, destination;
static struct k_thread worker;
K_THREAD_STACK_DEFINE(worker_stack, 2048);
K_SEM_DEFINE(entering, 0, 1);
K_SEM_DEFINE(finished, 0, 1);
static unsigned iterations;
static wl_err_t worker_result;
static bool worker_active;
static struct k_thread producers[PRODUCER_COUNT];
K_THREAD_STACK_ARRAY_DEFINE(producer_stacks, PRODUCER_COUNT, 1024);
K_SEM_DEFINE(producer_start, 0, PRODUCER_COUNT);
K_SEM_DEFINE(producer_entered, 0, PRODUCER_COUNT);
static atomic_t producers_done;
static wl_err_t producer_results[PRODUCER_COUNT];
static unsigned producers_active;

static struct net_sockaddr_in loopback(uint16_t port) {
  struct net_sockaddr_in address = {
    .sin_family = NET_AF_INET, .sin_port = net_htons(port),
    .sin_addr = NET_INADDR_LOOPBACK_INIT,
  };
  return address;
}

static struct net_sockaddr_in bound_address(int fd) {
  struct net_sockaddr_in address;
  net_socklen_t length = sizeof(address);
  zassert_ok(zsock_getsockname(fd, (struct net_sockaddr *)&address, &length));
  zassert_equal(length, sizeof(address));
  return address;
}

static int raw_socket(void) {
  int fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
  struct net_sockaddr_in local = loopback(0);
  zassert_true(fd >= 0, "socket failed: %d", errno);
  zassert_ok(zsock_bind(fd, (struct net_sockaddr *)&local, sizeof(local)));
  return fd;
}

static void send_packet(int fd, const uint8_t *data, size_t length) {
  zassert_equal(zsock_sendto(fd, data, length, ZSOCK_MSG_DONTWAIT,
      (struct net_sockaddr *)&destination, sizeof(destination)), (ssize_t)length);
}

static void before(void *unused) {
  struct net_sockaddr_in local = loopback(0);
  (void)unused;
  sender = raw_socket();
  peer = bound_address(sender);
  zassert_ok(wl_udp_socket_open(&io, &local, &peer));
  destination = bound_address(io.socket_fd);
  k_sem_reset(&entering);
  k_sem_reset(&finished);
  k_sem_reset(&producer_start);
  k_sem_reset(&producer_entered);
  atomic_clear(&producers_done);
}

static void after(void *unused) {
  (void)unused;
  if (producers_active != 0) {
    (void)wl_udp_socket_request_stop(&io);
    for (unsigned i = 0; i < producers_active; ++i) k_sem_give(&producer_start);
    for (unsigned i = 0; i < producers_active; ++i)
      zassert_ok(k_thread_join(&producers[i], K_SECONDS(2)));
    producers_active = 0;
  }
  if (worker_active) {
    (void)wl_udp_socket_request_stop(&io);
    zassert_ok(k_thread_join(&worker, K_SECONDS(2)));
    worker_active = false;
  }
  zassert_ok(wl_udp_socket_close(&io));
  zassert_ok(zsock_close(sender));
}

static void wait_worker(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  for (unsigned i = 0; i < iterations; ++i) {
    k_sem_give(&entering);
    worker_result = wl_udp_socket_wait(&io, true, UINT32_MAX);
    k_sem_give(&finished);
    if (worker_result != WL_OK) break;
  }
}

static void start_worker(unsigned count) {
  iterations = count;
  worker_result = WL_ERR_INVALID_STATE;
  worker_active = true;
  k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
      wait_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
}

static void join_worker(wl_err_t expected) {
  zassert_ok(k_thread_join(&worker, K_SECONDS(2)));
  worker_active = false;
  zassert_equal(worker_result, expected);
}

static void notify_producer(void *a, void *b, void *c) {
  wl_err_t *result = a;
  (void)b; (void)c;
  (void)k_sem_take(&producer_start, K_FOREVER);
  *result = wl_udp_socket_notify(&io);
  k_sem_give(&producer_entered);
  for (unsigned i = 0; i < 64 && *result == WL_OK; ++i) {
    *result = wl_udp_socket_notify(&io);
    if ((i % 8U) == 0) k_msleep(1);
  }
  atomic_inc(&producers_done);
  /* Publish completion before the final wake, covering the check/wait race. */
  if (*result == WL_OK) *result = wl_udp_socket_notify(&io);
}

static void start_producers(void) {
  for (unsigned i = 0; i < PRODUCER_COUNT; ++i) {
    producer_results[i] = WL_ERR_INVALID_STATE;
    k_thread_create(&producers[i], producer_stacks[i],
        K_THREAD_STACK_SIZEOF(producer_stacks[i]), notify_producer,
        &producer_results[i], NULL, NULL, 5, 0, K_NO_WAIT);
    ++producers_active;
  }
  for (unsigned i = 0; i < PRODUCER_COUNT; ++i) k_sem_give(&producer_start);
}

static void join_producers(bool stopped) {
  for (unsigned i = 0; i < PRODUCER_COUNT; ++i) {
    zassert_ok(k_thread_join(&producers[i], K_SECONDS(2)));
    zassert_true(producer_results[i] == WL_OK ||
        (stopped && producer_results[i] == WL_ERR_CANCELLED));
  }
  producers_active = 0;
  zassert_equal(atomic_get(&producers_done), PRODUCER_COUNT);
}

ZTEST(udp_socket_contract, test_multiple_producers_during_wait_and_drain) {
  start_producers();
  while (atomic_get(&producers_done) != PRODUCER_COUNT)
    zassert_ok(wl_udp_socket_wait(&io, true, 1000));
  join_producers(false);
  const wl_err_t final = wl_udp_socket_wait(&io, true, 0);
  zassert_true(final == WL_OK || final == WL_ERR_NO_DATA);
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
  zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
}

ZTEST(udp_socket_contract, test_stop_with_active_producers) {
  start_producers();
  for (unsigned i = 0; i < PRODUCER_COUNT; ++i)
    zassert_ok(k_sem_take(&producer_entered, K_SECONDS(2)));
  zassert_ok(wl_udp_socket_request_stop(&io));
  join_producers(true);
  zassert_equal(wl_udp_socket_wait(&io, true, UINT32_MAX), WL_ERR_CANCELLED);
}

ZTEST(udp_socket_contract, test_timeout_and_notification_coalescing) {
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
  const int64_t start = k_uptime_get();
  zassert_equal(wl_udp_socket_wait(&io, true, 3), WL_ERR_NO_DATA);
  zassert_true(k_uptime_get() - start >= 3);
  for (unsigned i = 0; i < 500; ++i) zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
  zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, true, UINT32_MAX));
  /* Overflow cannot lose a wake: the counter is already readable. */
  zassert_ok(zvfs_eventfd_write(io.wake_fd, UINT64_MAX - 1U));
  zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
}

ZTEST(udp_socket_contract, test_notification_races_wait_entry) {
  start_worker(NOTIFY_ROUNDS);
  for (unsigned i = 0; i < NOTIFY_ROUNDS; ++i) {
    zassert_ok(k_sem_take(&entering, K_SECONDS(2)));
    zassert_ok(wl_udp_socket_notify(&io));
    zassert_ok(k_sem_take(&finished, K_SECONDS(2)));
  }
  join_worker(WL_OK);
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
}

ZTEST(udp_socket_contract, test_stop_wakes_unbounded_wait_and_latches) {
  start_worker(1);
  zassert_ok(k_sem_take(&entering, K_SECONDS(2)));
  k_msleep(2);
  zassert_ok(wl_udp_socket_request_stop(&io));
  join_worker(WL_ERR_CANCELLED);
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_CANCELLED);
  zassert_equal(wl_udp_socket_notify(&io), WL_ERR_CANCELLED);
  zassert_ok(wl_udp_socket_request_stop(&io));
  uint8_t data[2];
  size_t length = 99;
  zassert_equal(wl_udp_socket_receive(&io, data, sizeof(data), 1, &length),
                WL_ERR_CANCELLED);
  zassert_equal(length, 0);
  zassert_equal(wl_udp_socket_send(&io, data, 1), WL_SINK_FAILED);
}

ZTEST(udp_socket_contract, test_socket_wakes_unbounded_wait) {
  uint8_t value = 42, output[2];
  size_t length;
  start_worker(1);
  zassert_ok(k_sem_take(&entering, K_SECONDS(2)));
  k_msleep(2);
  send_packet(sender, &value, 1);
  join_worker(WL_OK);
  /* Wait never consumes the datagram; readiness remains level-triggered. */
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
  zassert_ok(wl_udp_socket_receive(&io, output, sizeof(output), 1, &length));
  zassert_equal(length, 1);
  zassert_equal(output[0], value);
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
}

ZTEST(udp_socket_contract, test_socket_and_notification_ready_together) {
  uint8_t value = 9, output[2];
  size_t length;
  send_packet(sender, &value, 1);
  zassert_ok(wl_udp_socket_wait(&io, true, 1000));
  zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
  zassert_ok(wl_udp_socket_receive(&io, output, sizeof(output), 1, &length));
  zassert_equal(length, 1);
  zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
}

ZTEST(udp_socket_contract, test_full_queue_can_mask_rx_readiness) {
  uint8_t value = 9;
  send_packet(sender, &value, 1);
  zassert_ok(wl_udp_socket_wait(&io, true, 1000));
  zassert_equal(wl_udp_socket_wait(&io, false, 2), WL_ERR_NO_DATA);
  zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, false, 0));
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
}

ZTEST(udp_socket_contract, test_empty_and_foreign_datagrams_are_consumed) {
  uint8_t buffer[FRAME_BOUND + 1], value = 11;
  size_t length = 99;
  send_packet(sender, &value, 0);
  zassert_ok(wl_udp_socket_wait(&io, true, 1000));
  zassert_equal(wl_udp_socket_receive(&io, buffer, sizeof(buffer), FRAME_BOUND,
      &length), WL_ERR_BAD_FRAME);
  zassert_equal(length, 0);
  int foreign = raw_socket();
  send_packet(foreign, &value, 1);
  zassert_ok(wl_udp_socket_wait(&io, true, 1000));
  zassert_equal(wl_udp_socket_receive(&io, buffer, sizeof(buffer), FRAME_BOUND,
      &length), WL_ERR_BAD_FRAME);
  zassert_equal(length, 0);
  zassert_ok(zsock_close(foreign));
  zassert_equal(wl_udp_socket_receive(&io, buffer, sizeof(buffer), FRAME_BOUND,
      &length), WL_ERR_NO_DATA);
  /* Same source port, wrong IPv4 address: port filtering alone is insufficient. */
  struct net_sockaddr_in local = loopback(0), different_ip = peer;
  different_ip.sin_addr.s_addr = net_htonl(0x7f000002U);
  zassert_ok(wl_udp_socket_close(&io));
  zassert_ok(wl_udp_socket_open(&io, &local, &different_ip));
  destination = bound_address(io.socket_fd);
  send_packet(sender, &value, 1);
  zassert_ok(wl_udp_socket_wait(&io, true, 1000));
  zassert_equal(wl_udp_socket_receive(&io, buffer, sizeof(buffer), FRAME_BOUND,
      &length), WL_ERR_BAD_FRAME);
  zassert_equal(length, 0);
}

ZTEST(udp_socket_contract, test_maximum_and_truncated_datagrams) {
  uint8_t input[FRAME_BOUND * 2];
  struct { uint8_t data[FRAME_BOUND + 1]; uint8_t canary[16]; } output;
  const size_t lengths[] = {1, FRAME_BOUND, FRAME_BOUND + 1, sizeof(input)};
  for (size_t i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)(i * 7U + 1U);
  for (size_t i = 0; i < ARRAY_SIZE(lengths); ++i) {
    size_t received = 99;
    memset(&output, 0xa5, sizeof(output));
    send_packet(sender, input, lengths[i]);
    zassert_ok(wl_udp_socket_wait(&io, true, 1000));
    const int result = wl_udp_socket_receive(&io, output.data, sizeof(output.data),
                                             FRAME_BOUND, &received);
    if (lengths[i] <= FRAME_BOUND) {
      zassert_ok(result);
      zassert_equal(received, lengths[i]);
      zassert_mem_equal(output.data, input, received);
    } else {
      zassert_equal(result, WL_ERR_FRAME_TOO_LONG);
      zassert_equal(received, 0);
    }
    for (size_t j = 0; j < sizeof(output.canary); ++j)
      zassert_equal(output.canary[j], 0xa5);
    zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
  }
}

ZTEST(udp_socket_contract, test_invalid_receive_capacity_does_not_consume) {
  uint8_t input = 7, output[2];
  size_t length = 99;
  send_packet(sender, &input, 1);
  zassert_ok(wl_udp_socket_wait(&io, true, 1000));
  zassert_equal(wl_udp_socket_receive(&io, output, 1, 1, &length),
                WL_ERR_INVALID_ARG);
  zassert_equal(length, 0);
  zassert_ok(wl_udp_socket_receive(&io, output, 2, 1, &length));
  zassert_equal(output[0], input);
}

ZTEST(udp_socket_contract, test_send_and_error_classification) {
  const uint8_t input[] = {1, 2, 3, 4};
  uint8_t output[sizeof(input)];
  zassert_equal(wl_udp_socket_send(&io, input, sizeof(input)), WL_SINK_SENT);
  struct zsock_pollfd fd = {.fd = sender, .events = ZSOCK_POLLIN};
  zassert_equal(zsock_poll(&fd, 1, 1000), 1);
  zassert_equal(zsock_recv(sender, output, sizeof(output), ZSOCK_MSG_DONTWAIT),
                sizeof(output));
  zassert_mem_equal(output, input, sizeof(input));
  const int transient[] = {EAGAIN, EWOULDBLOCK, ENOBUFS, ENOMEM, EINTR};
  const int fatal[] = {EMSGSIZE, EBADF, EINVAL, ENETUNREACH, EIO, ECONNREFUSED};
  for (size_t i = 0; i < ARRAY_SIZE(transient); ++i)
    zassert_equal(wl_udp_socket_classify_send(-1, 4, transient[i]), WL_SINK_BUSY);
  for (size_t i = 0; i < ARRAY_SIZE(fatal); ++i)
    zassert_equal(wl_udp_socket_classify_send(-1, 4, fatal[i]), WL_SINK_FAILED);
  zassert_equal(wl_udp_socket_classify_send(4, 4, 0), WL_SINK_SENT);
  zassert_equal(wl_udp_socket_classify_send(3, 4, 0), WL_SINK_FAILED);
  zassert_equal(wl_udp_socket_classify_send(0, 4, 0), WL_SINK_FAILED);
  zassert_equal(wl_udp_socket_classify_send(0, 0, 0), WL_SINK_FAILED);
}

ZTEST(udp_socket_contract, test_tx_packet_pool_pressure_and_recovery) {
  struct net_pkt *held[CONFIG_NET_PKT_TX_COUNT];
  size_t count = 0;
  const uint8_t value = 42;
  uint8_t output;
  while (count < ARRAY_SIZE(held)) {
    struct net_pkt *packet = net_pkt_alloc(K_NO_WAIT);
    if (packet == NULL) break;
    held[count++] = packet;
  }
  bool all_busy = true;
  for (unsigned i = 0; i < 20; ++i) {
    if (wl_udp_socket_send(&io, &value, 1) != WL_SINK_BUSY) all_busy = false;
  }
  /* Release the real TX packet pool before asserting so failures cannot leak. */
  for (size_t i = 0; i < count; ++i) net_pkt_unref(held[i]);
  zassert_equal(count, ARRAY_SIZE(held));
  zassert_true(all_busy);
  zassert_equal(zsock_recv(sender, &output, 1, ZSOCK_MSG_DONTWAIT), -1);
  zassert_true(errno == EAGAIN || errno == EWOULDBLOCK);
  zassert_equal(wl_udp_socket_send(&io, &value, 1), WL_SINK_SENT);
  struct zsock_pollfd fd = {.fd = sender, .events = ZSOCK_POLLIN};
  zassert_equal(zsock_poll(&fd, 1, 1000), 1);
  zassert_equal(zsock_recv(sender, &output, 1, ZSOCK_MSG_DONTWAIT), 1);
  zassert_equal(output, value);
  zassert_equal(zsock_recv(sender, &output, 1, ZSOCK_MSG_DONTWAIT), -1);
  zassert_true(errno == EAGAIN || errno == EWOULDBLOCK);
}

ZTEST(udp_socket_contract, test_open_failure_leaves_original_and_candidate_intact) {
  wl_udp_socket_t other = WL_UDP_SOCKET_INITIALIZER;
  struct net_sockaddr_in local = loopback(0), invalid = peer;
  const int original_fd = io.socket_fd;
  zassert_equal(wl_udp_socket_open(&io, &local, &peer), WL_ERR_INVALID_STATE);
  zassert_equal(io.socket_fd, original_fd);
  invalid.sin_port = 0;
  zassert_equal(wl_udp_socket_open(&other, &local, &invalid), WL_ERR_INVALID_ARG);
  for (unsigned i = 0; i < 20; ++i)
    zassert_equal(wl_udp_socket_open(&other, &destination, &peer), WL_ERR_IO);
  zassert_false(other.opened);
  zassert_equal(other.socket_fd, -1);
  zassert_equal(other.wake_fd, -1);
  zassert_ok(wl_udp_socket_open(&other, &local, &peer));
  zassert_ok(wl_udp_socket_close(&other));
  zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
}

ZTEST(udp_socket_contract, test_eventfd_exhaustion_rolls_back_socket_allocation) {
#ifdef ZVFS_EVENTFD_SIZE
  int held[ZVFS_EVENTFD_SIZE];
#else
  int held[CONFIG_ZVFS_EVENTFD_MAX];
#endif
  size_t count = 0;
  wl_udp_socket_t other = WL_UDP_SOCKET_INITIALIZER;
  struct net_sockaddr_in local = loopback(0);
  /* Exhaust the real bounded eventfd pool, then fail after UDP bind. */
  while (count < ARRAY_SIZE(held)) {
    int fd = zvfs_eventfd(0, ZVFS_EFD_NONBLOCK);
    if (fd < 0) break;
    held[count++] = fd;
  }
  zassert_equal(count + 1, ARRAY_SIZE(held));
  for (unsigned i = 0; i < 20; ++i)
    zassert_equal(wl_udp_socket_open(&other, &local, &peer), WL_ERR_NO_MEM);
  zassert_false(other.opened);
  for (size_t i = 0; i < count; ++i) zassert_ok(zvfs_close(held[i]));
  zassert_ok(wl_udp_socket_open(&other, &local, &peer));
  zassert_ok(wl_udp_socket_close(&other));
}

ZTEST(udp_socket_contract, test_close_and_reopen_has_no_stale_stop_or_notify) {
  struct net_sockaddr_in local = loopback(0);
  for (unsigned i = 0; i < 32; ++i) {
    zassert_ok(wl_udp_socket_notify(&io));
    zassert_ok(wl_udp_socket_request_stop(&io));
    zassert_ok(wl_udp_socket_close(&io));
    zassert_ok(wl_udp_socket_close(&io));
    zassert_equal(wl_udp_socket_notify(&io), WL_ERR_NOT_INITIALIZED);
    zassert_ok(wl_udp_socket_open(&io, &local, &peer));
    zassert_equal(wl_udp_socket_wait(&io, true, 0), WL_ERR_NO_DATA);
  }
}

static wl_err_t irq_notify, irq_stop, irq_wait;
static void interrupt_probe(const void *unused) {
  (void)unused;
  irq_notify = wl_udp_socket_notify(&io);
  irq_stop = wl_udp_socket_request_stop(&io);
  irq_wait = wl_udp_socket_wait(&io, true, 0);
}

ZTEST(udp_socket_contract, test_isr_use_rejected_before_mutex_socket_operations) {
  irq_offload(interrupt_probe, NULL);
  zassert_equal(irq_notify, WL_ERR_REENTRANT);
  zassert_equal(irq_stop, WL_ERR_REENTRANT);
  zassert_equal(irq_wait, WL_ERR_REENTRANT);
  zassert_false(atomic_get(&io.stopped));
  zassert_ok(wl_udp_socket_notify(&io));
  zassert_ok(wl_udp_socket_wait(&io, true, 0));
}

static wl_ctx_t link;
static uint8_t tx_payload[64], tx_unit[256], control[256], fallback[256];
static uint8_t slots[2 * (FRAME_BOUND + 1)];

ZTEST(udp_socket_contract, test_receive_into_core_claim_commit_and_abort) {
  const wl_config_t config = {.max_payload_len = sizeof(tx_payload),
    .envelope = WL_ENVELOPE_NATIVE_PACKET, .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 1, .ack_timeout_ms = 10};
  const wl_storage_t storage = {.tx_payload = tx_payload,
    .tx_payload_size = sizeof(tx_payload), .tx_unit = tx_unit,
    .tx_unit_size = sizeof(tx_unit), .control_unit = control,
    .control_unit_size = sizeof(control), .rx_fallback = fallback,
    .rx_fallback_size = sizeof(fallback)};
  const wl_rx_unit_queue_config_t queue = {.storage = slots,
    .storage_size = sizeof(slots), .unit_size = FRAME_BOUND + 1, .slot_count = 2};
  const uint8_t payload[] = {10, 20, 30};
  const wl_wire_packet_t packet = {.type = WL_PACKET_DATA,
    .integrity = WL_INTEGRITY_CRC32C, .message_id = 42,
    .payload = payload, .payload_len = sizeof(payload)};
  uint8_t wire[FRAME_BOUND];
  size_t wire_size, length;
  wl_rx_unit_claim_t claim;
  wl_event_t event;
  zassert_ok(wl_init(&link, &config, &storage));
  zassert_ok(wl_rx_unit_queue_init(&link, &queue));
  zassert_ok(wl_frame_encode(&packet, config.envelope, wire, sizeof(wire), &wire_size));
  zassert_ok(wl_rx_unit_claim(&link, FRAME_BOUND + 1, &claim));
  zassert_equal(wl_udp_socket_receive(&io, claim.span.data, claim.span.length,
      FRAME_BOUND, &length), WL_ERR_NO_DATA);
  zassert_ok(wl_rx_unit_abort(&link, &claim));
  uint8_t oversized[FRAME_BOUND + 1] = {0};
  const size_t invalid_lengths[] = {0, sizeof(oversized)};
  for (size_t i = 0; i < ARRAY_SIZE(invalid_lengths); ++i) {
    send_packet(sender, oversized, invalid_lengths[i]);
    zassert_ok(wl_udp_socket_wait(&io, true, 1000));
    zassert_ok(wl_rx_unit_claim(&link, FRAME_BOUND + 1, &claim));
    zassert_equal(wl_udp_socket_receive(&io, claim.span.data, claim.span.length,
        FRAME_BOUND, &length), i == 0 ? WL_ERR_BAD_FRAME : WL_ERR_FRAME_TOO_LONG);
    zassert_equal(length, 0);
    zassert_ok(wl_rx_unit_abort(&link, &claim));
    zassert_equal(wl_poll(&link, 0, &event), WL_ERR_NO_DATA);
  }
  for (unsigned i = 0; i < 2; ++i) {
    send_packet(sender, wire, wire_size);
    zassert_ok(wl_udp_socket_wait(&io, true, 1000));
    zassert_ok(wl_rx_unit_claim(&link, FRAME_BOUND + 1, &claim));
    zassert_ok(wl_udp_socket_receive(&io, claim.span.data, claim.span.length,
        FRAME_BOUND, &length));
    zassert_mem_equal(claim.span.data, wire, wire_size);
    zassert_ok(wl_rx_unit_commit(&link, &claim, length));
  }
  zassert_equal(wl_rx_unit_claim(&link, FRAME_BOUND + 1, &claim), WL_ERR_WOULD_BLOCK);
  for (unsigned i = 0; i < 2; ++i) {
    zassert_ok(wl_poll(&link, 0, &event));
    zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
    zassert_mem_equal(event.payload, payload, sizeof(payload));
    wl_event_release(&link, &event);
  }
  zassert_ok(wl_rx_unit_claim(&link, FRAME_BOUND + 1, &claim));
  zassert_ok(wl_rx_unit_abort(&link, &claim));
  zassert_equal(wl_poll(&link, 0, &event), WL_ERR_NO_DATA);
}

ZTEST_SUITE(udp_socket_contract, NULL, NULL, before, after, NULL);
