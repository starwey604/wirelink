/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/kernel_udp.h"
#include "wirelink/packet_stream.h"
#include "wirelink/frame.h"
#include "kernel_udp_socket_contract.h"
#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>
static wl_kernel_udp_t adapter;
static wl_packet_port_t port;
static uint8_t rx[2][96];
static int sender, foreign;
static struct net_sockaddr_in destination;
static struct net_sockaddr_in address(uint16_t p) {
  struct net_sockaddr_in a = {
      .sin_family = NET_AF_INET, .sin_port = net_htons(p), .sin_addr = NET_INADDR_LOOPBACK_INIT};
  return a;
}
static struct net_sockaddr_in bound(int fd) {
  struct net_sockaddr_in a;
  net_socklen_t n = sizeof(a);
  zassert_ok(zsock_getsockname(fd, (struct net_sockaddr *)&a, &n));
  return a;
}
static int socket_open(void) {
  int fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
  zassert_true(fd >= 0);
  struct net_sockaddr_in a = address(0);
  zassert_ok(zsock_bind(fd, (struct net_sockaddr *)&a, sizeof(a)));
  return fd;
}
static uint32_t now(void *user) {
  (void)user;
  return k_uptime_get_32();
}
static uint64_t instance;
static void setup(bool cobs) {
  memset(&adapter, 0, sizeof(adapter));
  sender = socket_open();
  foreign = socket_open();
  wl_packet_config_t pc = {.instance = ++instance,
                           .rx_storage = &rx[0][0],
                           .rx_unit_size = 96,
                           .rx_storage_size = sizeof(rx),
                           .submit = wl_kernel_udp_submit,
                           .notify = wl_kernel_udp_notify,
                           .user = &adapter};
  zassert_ok(wl_packet_init(&port, &pc));
  wl_kernel_udp_config_t config = {.port = &port,
                                   .local = address(0),
                                   .peer = bound(sender),
                                   .maximum_datagram_size = 64,
                                   .service_budget = 4,
                                   .clock = {now, NULL},
                                   .decode = cobs ? wl_packet_cobs_crc16_decode : NULL};
  zassert_ok(wl_kernel_udp_open(&adapter, &config));
  destination = address(wl_kernel_udp_local_port(&adapter));
  zassert_true(destination.sin_port != 0);
}
static void send_bytes(int fd, const uint8_t *data, size_t n) {
  zassert_equal(zsock_sendto(fd, data, n, ZSOCK_MSG_DONTWAIT, (struct net_sockaddr *)&destination,
                             sizeof(destination)),
                (ssize_t)n);
}
static void service_until(unsigned count) {
  for (unsigned i = 0; i < 100 && adapter.received < count; ++i) {
    zassert_ok(wl_kernel_udp_wait(&adapter, 10));
    zassert_ok(wl_kernel_udp_service(&adapter, now(NULL)));
  }
  zassert_equal(adapter.received, count);
}
static void cleanup(void) {
  zassert_ok(wl_kernel_udp_close(&adapter));
  zassert_ok(zsock_close(sender));
  zassert_ok(zsock_close(foreign));
  zassert_true(wl_packet_quiescent(&port));
}
static struct k_thread producer;
K_THREAD_STACK_DEFINE(producer_stack, 1024);
static void notify_later(void *a, void *b, void *c) {
  (void)a;
  (void)b;
  (void)c;
  k_sleep(K_MSEC(15));
  wl_kernel_udp_notify(&adapter);
}
ZTEST(static_udp, test_guard_peer_slots_and_latched_wait) {
  setup(false);
  const uint8_t packet[] = {0xa4, 0, 0, 22, 1, 2, 3};
  send_bytes(foreign, packet, sizeof(packet));
  send_bytes(sender, NULL, 0);
  uint8_t large[80] = {0};
  send_bytes(sender, large, sizeof(large));
  for (unsigned i = 0; i < 3; ++i)
    send_bytes(sender, packet, sizeof(packet));
  service_until(2);
  zassert_equal(adapter.rejected, 3);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&port, &borrow));
  zassert_mem_equal(borrow.data, packet, sizeof(packet));
  zassert_false(wl_kernel_udp_readiness(&adapter, now(NULL)).runnable);
  // Full RX masks the socket, including a queued third packet. Wait for notify.
  k_thread_create(&producer, producer_stack, K_THREAD_STACK_SIZEOF(producer_stack), notify_later,
                  NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  // Consume existing publications before checking the delayed notification.
  while (wl_kernel_udp_wait(&adapter, 0) == WL_OK) {
  }
  uint32_t begin = now(NULL);
  zassert_ok(wl_kernel_udp_wait(&adapter, 100));
  zassert_true(now(NULL) - begin >= 5);
  zassert_ok(k_thread_join(&producer, K_SECONDS(1)));
  zassert_ok(wl_packet_rx_release(&port, &borrow));
  service_until(3);
  for (unsigned i = 0; i < 2; ++i) {
    zassert_ok(wl_packet_rx_borrow(&port, &borrow));
    zassert_ok(wl_packet_rx_release(&port, &borrow));
  }
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&port, packet, sizeof(packet), &token));
  zassert_equal(wl_packet_tx_take(&port, &token), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  uint8_t result[32];
  int n = -1;
  for (unsigned i = 0; i < 100 && n < 0; ++i) {
    n = zsock_recv(sender, result, sizeof(result), ZSOCK_MSG_DONTWAIT);
    if (n < 0)
      k_sleep(K_MSEC(1));
  }
  zassert_equal(n, sizeof(packet));
  zassert_mem_equal(result, packet, sizeof(packet));
  zassert_equal(wl_kernel_udp_socket_classify_send(-1, 7, EAGAIN), WL_PACKET_BUSY);
  zassert_equal(wl_kernel_udp_socket_classify_send(6, 7, 0), WL_PACKET_FAILED);
  zassert_ok(wl_kernel_udp_request_stop(&adapter));
  zassert_equal(wl_kernel_udp_wait(&adapter, 0), WL_ERR_CANCELLED);
  cleanup();
}
ZTEST(static_udp, test_selected_cobs_decode_and_recovery) {
  setup(true);
  uint8_t payload[] = {0, 1, 2, 3};
  uint8_t wire[64];
  size_t n = 0;
  wl_wire_packet_t packet = {.type = WL_PACKET_DATA,
                             .message_id = 22,
                             .integrity = WL_INTEGRITY_CRC16,
                             .payload = payload,
                             .payload_len = sizeof(payload)};
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, wire, sizeof(wire), &n));
  wire[2] ^= 1;
  send_bytes(sender, wire, n);
  wire[2] ^= 1;
  send_bytes(sender, wire, n);
  service_until(1);
  zassert_equal(adapter.rejected, 1);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&port, &borrow));
  zassert_equal(borrow.length, 8);
  zassert_mem_equal(borrow.data + 4, payload, sizeof(payload));
  zassert_ok(wl_packet_rx_release(&port, &borrow));
  cleanup();
}
ZTEST_SUITE(static_udp, NULL, NULL, NULL, NULL, NULL);
