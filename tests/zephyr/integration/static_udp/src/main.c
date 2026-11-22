/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/udp.h"
#include "wirelink/node.h"
#include "wirelink/wire.h"
#include "wirelink/cobs.h"
#include "wirelink/crc.h"
#include "udp_socket_contract.h"
#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>
static wl_zephyr_udp_t adapter;
static wl_node_t node;
static uint8_t rx[WL_PACKET_RX_SLOTS][96];
static wl_packet_stream_t stream;
static uint8_t tx_wire[96];
static const wl_node_stream_ops_t cobs_ops = {
    wl_packet_stream_cobs_crc16_init, wl_packet_stream_cobs_crc16_submit,
    wl_packet_stream_cobs_crc16_feed, wl_packet_stream_abort_rx, wl_packet_stream_notify,
    WL_NODE_COBS_CRC16};
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
static uint32_t instance;
static void setup(bool cobs) {
  memset(&adapter, 0, sizeof(adapter));
  sender = socket_open();
  foreign = socket_open();
  wl_zephyr_udp_config_t config = {.local = address(0),
                                   .peer = bound(sender),
                                   .service_budget = 4,
                                   .clock = {now, NULL}};
  wl_node_layout_t layout = {.instance = ++instance, .rx_storage = rx[0], .rx_storage_size = sizeof(rx),
      .rx_frame_max = 64U, .rx_wire_max = 64U, .tx_frame_max = 64U, .tx_wire_max = 64U, .has_tx = 1U};
  if (cobs) {
    layout.stream = &stream;
    layout.stream_ops = &cobs_ops;
    layout.stream_tx_wire = (wl_span_t){tx_wire, sizeof(tx_wire)};
    layout.rx_wire_max = layout.tx_wire_max = wl_packet_stream_cobs_crc16_bound(64U);
  }
  zassert_ok(wl_node_bind_transport(&node, &layout, wl_zephyr_udp_transport(&adapter, &config)));
  destination = address(wl_zephyr_udp_local_port(&adapter));
  zassert_true(destination.sin_port != 0);
}
static void send_bytes(int fd, const uint8_t *data, size_t n) {
  zassert_equal(zsock_sendto(fd, data, n, ZSOCK_MSG_DONTWAIT, (struct net_sockaddr *)&destination,
                             sizeof(destination)),
                (ssize_t)n);
}
static void service_until(unsigned count) {
  for (unsigned i = 0; i < 100 && adapter.received < count; ++i) {
    zassert_ok(wl_zephyr_udp_wait(&adapter, 10));
    zassert_ok(wl_zephyr_udp_service(&adapter, now(NULL)));
  }
  zassert_equal(adapter.received, count);
}
static void cleanup(void) {
  zassert_ok(wl_node_close(&node, now(NULL)));
  zassert_ok(zsock_close(sender));
  zassert_ok(zsock_close(foreign));
  zassert_true(wl_packet_quiescent(&node.port));
}
static struct k_thread producer;
K_THREAD_STACK_DEFINE(producer_stack, 1024);
static void notify_later(void *a, void *b, void *c) {
  (void)a;
  (void)b;
  (void)c;
  k_sleep(K_MSEC(15));
  wl_zephyr_udp_notify(&adapter);
}
ZTEST(static_udp, test_guard_peer_slots_and_latched_wait) {
  setup(false);
  const uint8_t packet[] = {0x57, 0, 22, 0, 1, 2, 3};
  send_bytes(foreign, packet, sizeof(packet));
  send_bytes(sender, NULL, 0);
  uint8_t large[80] = {0};
  send_bytes(sender, large, sizeof(large));
  for (unsigned i = 0; i < 3; ++i)
    send_bytes(sender, packet, sizeof(packet));
  service_until(2);
  zassert_equal(adapter.rejected, 3);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&node.port, &borrow));
  zassert_mem_equal(borrow.data, packet, sizeof(packet));
  zassert_false(wl_zephyr_udp_readiness(&adapter, now(NULL)).runnable);
  // Full RX masks the socket, including a queued third packet. Wait for notify.
  k_thread_create(&producer, producer_stack, K_THREAD_STACK_SIZEOF(producer_stack), notify_later,
                  NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  // Consume existing publications before checking the delayed notification.
  while (wl_zephyr_udp_wait(&adapter, 0) == WL_OK) {
  }
  uint32_t begin = now(NULL);
  zassert_ok(wl_zephyr_udp_wait(&adapter, 100));
  zassert_true(now(NULL) - begin >= 5);
  zassert_ok(k_thread_join(&producer, K_SECONDS(1)));
  zassert_ok(wl_packet_rx_release(&node.port, &borrow));
  service_until(3);
  for (unsigned i = 0; i < 2; ++i) {
    zassert_ok(wl_packet_rx_borrow(&node.port, &borrow));
    zassert_ok(wl_packet_rx_release(&node.port, &borrow));
  }
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&node.port, packet, sizeof(packet), &token));
  zassert_equal(wl_packet_tx_take(&node.port, &token), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  uint8_t result[32];
  int n = -1;
  for (unsigned i = 0; i < 100 && n < 0; ++i) {
    n = zsock_recv(sender, result, sizeof(result), ZSOCK_MSG_DONTWAIT);
    if (n < 0)
      k_sleep(K_MSEC(1));
  }
  zassert_equal(n, sizeof(packet));
  zassert_mem_equal(result, packet, sizeof(packet));
  zassert_equal(wl_zephyr_udp_socket_classify_send(-1, 7, EAGAIN), WL_PACKET_BUSY);
  zassert_equal(wl_zephyr_udp_socket_classify_send(6, 7, 0), WL_PACKET_FAILED);
  zassert_ok(wl_zephyr_udp_request_stop(&adapter));
  zassert_equal(wl_zephyr_udp_wait(&adapter, 0), WL_ERR_CANCELLED);
  cleanup();
}
ZTEST(static_udp, test_selected_cobs_decode_and_recovery) {
  setup(true);
  uint8_t payload[] = {0, 1, 2, 3};
  uint8_t raw[WL_WIRE_NATIVE_HEADER + sizeof(payload) + 2U];
  uint8_t wire[64];
  size_t n = 0;
  zassert_ok(wl_wire_native_none_seal(raw, WL_WIRE_NATIVE_HEADER + sizeof(payload), 22U,
                                        sizeof(payload)));
  memcpy(raw + WL_WIRE_NATIVE_HEADER, payload, sizeof(payload));
  const uint16_t crc = wl_crc16_ccitt_false(raw, WL_WIRE_NATIVE_HEADER + sizeof(payload));
  raw[WL_WIRE_NATIVE_HEADER + sizeof(payload)] = (uint8_t)(crc >> 8U);
  raw[WL_WIRE_NATIVE_HEADER + sizeof(payload) + 1U] = (uint8_t)crc;
  zassert_ok(wl_cobs_encode(raw, sizeof(raw), wire, sizeof(wire) - 1U, &n));
  wire[n++] = 0U;
  wire[2] ^= 1;
  send_bytes(sender, wire, n);
  wire[2] ^= 1;
  send_bytes(sender, wire, n);
  service_until(1);
  zassert_equal(adapter.rejected, 1);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&node.port, &borrow));
  zassert_equal(borrow.length, 8);
  zassert_mem_equal(borrow.data + 4, payload, sizeof(payload));
  zassert_ok(wl_packet_rx_release(&node.port, &borrow));
  cleanup();
}
ZTEST_SUITE(static_udp, NULL, NULL, NULL, NULL, NULL);
