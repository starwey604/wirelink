/* SPDX-License-Identifier: Apache-2.0 */
#include "fixture.h"

NET_BUF_POOL_FIXED_DEFINE(stale_pool, 1, 0,
    sizeof(struct udc_buf_info) + sizeof(wl_packet_token_t), NULL);
static uint8_t stale_data[1];
static unsigned wakes;
static void wake(void *user) { ARG_UNUSED(user); ++wakes; }

static void init(wl_packet_port_t *port, wl_kernel_usb_bulk_t *adapter,
                  uint8_t *rx, uint64_t instance) {
  wl_packet_config_t config = {.instance = instance, .rx_storage = rx,
      .rx_unit_size = 2048U, .rx_storage_size = 4096U,
      .submit = wl_kernel_usb_bulk_submit, .notify = wl_kernel_usb_bulk_notify,
      .user = adapter};
  zassert_ok(wl_packet_init(port, &config));
  wl_kernel_usb_bulk_config_t usb = {.port = port, .maximum_frame_size = 1036U,
      .wake_consumer = wake};
  zassert_ok(wl_kernel_usb_bulk_init(adapter, &usb));
}

static struct net_buf *complete(struct usbd_class_data *c,
                                wl_kernel_usb_bulk_t *adapter,
                                struct net_buf *buffer, size_t length, int error) {
  net_buf_add(buffer, length);
  zassert_ok(c->api->request(c, buffer, error));
  int result = wl_kernel_usb_bulk_service(adapter);
  zassert_true(result == WL_OK || result == WL_ERR_IO || result == WL_ERR_WOULD_BLOCK);
  return take_queued(0x02U);
}

ZTEST(static_usb_bulk, test_real_class_ownership_and_recovery) {
  static wl_packet_port_t port;
  static wl_kernel_usb_bulk_t adapter;
  static uint8_t rx[2][2048];
  static uint8_t tx[64];
  static struct usbd_context context;
  struct usbd_class_data *c = bulk_class();
  zassert_not_null(c);
  c->uds_ctx = &context;
  context.status.speed = USBD_SPEED_FS;
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, false), 0x02U, 64U);
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, true), 0x83U, 64U);
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_HS, false), 0x04U, 512U);
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_HS, true), 0x85U, 512U);
  init(&port, &adapter, &rx[0][0], 17U);
  zassert_ok(c->api->init(c));
  c->api->enable(c);
  zassert_is_null(take_queued(0x02U), "enable must only publish work to owner");
  zassert_true(wl_kernel_usb_bulk_ready(&adapter));
  complete_out_inline = true;
  zassert_ok(wl_kernel_usb_bulk_service(&adapter));
  zassert_true(wl_kernel_usb_bulk_ready(&adapter), "inline completion must retain rearm");
  zassert_ok(wl_kernel_usb_bulk_service(&adapter));
  struct net_buf *out = take_queued(0x02U);
  zassert_not_null(out);
  zassert_equal(net_buf_tailroom(out), 2048U);
  memset(out->data, 0x5a, 64U);
  out = complete(c, &adapter, out, 64U, 0);
  zassert_not_null(out);
  memset(out->data, 0xa5, 24U);
  out = complete(c, &adapter, out, 24U, 0);
  zassert_is_null(out);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&port, &borrow));
  zassert_equal(borrow.data[0], 0x5aU);
  zassert_false(wl_kernel_usb_bulk_ready(&adapter), "borrowed/full RX must sleep");
  zassert_equal(wl_kernel_usb_bulk_service(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_ok(wl_packet_rx_release(&port, &borrow));
  zassert_true(wl_kernel_usb_bulk_ready(&adapter));
  zassert_ok(wl_kernel_usb_bulk_service(&adapter));
  out = take_queued(0x02U); zassert_not_null(out);
  zassert_ok(wl_packet_rx_borrow(&port, &borrow));
  zassert_equal(borrow.data[0], 0xa5U);
  zassert_ok(wl_packet_rx_release(&port, &borrow));

  wl_packet_token_t token, event;
  zassert_ok(wl_packet_tx_submit(&port, tx, sizeof(tx), &token));
  struct net_buf *in = take_queued(0x83U); zassert_not_null(in);
  zassert_true(udc_get_buf_info(in)->zlp);
  zassert_equal(in->data, tx, "USB must borrow the final frame, not copy it");
  zassert_ok(c->api->request(c, in, 0));
  zassert_true(wl_packet_tx_ready(&port));
  zassert_equal(wl_packet_tx_take(&port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  zassert_equal(event.instance, token.instance);
  zassert_equal(event.sequence, token.sequence);

  /* A matching endpoint with an old IO token cannot end a new transfer. */
  wl_packet_token_t stale_token = token;
  zassert_ok(wl_packet_tx_submit(&port, tx, 24U, &token));
  in = take_queued(0x83U); zassert_not_null(in);
  zassert_false(udc_get_buf_info(in)->zlp);
  struct net_buf *stale = net_buf_alloc_with_data(&stale_pool, stale_data, 1U, K_NO_WAIT);
  zassert_not_null(stale);
  udc_get_buf_info(stale)->ep = 0x83U;
  memcpy((uint8_t *)net_buf_user_data(stale) + sizeof(struct udc_buf_info),
         &stale_token, sizeof(stale_token));
  zassert_equal(c->api->request(c, stale, 0), -EINVAL);
  zassert_true(wl_packet_tx_active(&port));
  zassert_false(wl_packet_tx_ready(&port));
  zassert_ok(c->api->request(c, in, 0));
  zassert_equal(wl_packet_tx_take(&port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);

  /* Reconfigure before the old OUT cancellation arrives. */
  c->api->disable(c);
  context.status.speed = USBD_SPEED_HS;
  c->api->enable(c);
  zassert_ok(wl_kernel_usb_bulk_service(&adapter));
  zassert_ok(c->api->request(c, out, -ECONNABORTED));
  zassert_ok(wl_kernel_usb_bulk_service(&adapter));
  out = take_queued(0x04U); zassert_not_null(out);
  zassert_equal(net_buf_tailroom(out), 2048U);
  zassert_ok(wl_packet_tx_submit(&port, tx, sizeof(tx), &token));
  in = take_queued(0x85U); zassert_not_null(in);
  zassert_false(udc_get_buf_info(in)->zlp, "HS must use its assigned MPS");
  zassert_ok(c->api->request(c, in, 0));
  zassert_equal(wl_packet_tx_take(&port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);

  /* Full read loses the terminating boundary and requires real reconfigure. */
  net_buf_add(out, 2048U);
  zassert_ok(c->api->request(c, out, 0));
  zassert_equal(wl_kernel_usb_bulk_rx_fault(&adapter), WL_KERNEL_USB_RX_FAULT_BOUNDARY_LOST);
  zassert_equal(wl_kernel_usb_bulk_service(&adapter), WL_ERR_IO);
  zassert_false(wl_packet_rx_ready(&port));
  zassert_is_null(take_queued(0x04U));
  zassert_ok(wl_kernel_usb_bulk_recover(&adapter));
  zassert_equal(disable_calls, 1U); zassert_equal(enable_calls, 1U);
  zassert_equal(wl_kernel_usb_bulk_rx_fault(&adapter), WL_KERNEL_USB_RX_FAULT_BOUNDARY_LOST);
  context.status.speed = USBD_SPEED_FS;
  c->api->enable(c);
  zassert_equal(wl_kernel_usb_bulk_rx_fault(&adapter), WL_KERNEL_USB_RX_FAULT_NONE);
  zassert_ok(wl_kernel_usb_bulk_service(&adapter));
  out = take_queued(0x02U); zassert_not_null(out);

  /* stop is asynchronous: preserve both claims until cancellation returns. */
  zassert_ok(wl_packet_tx_submit(&port, tx, sizeof(tx), &token));
  in = take_queued(0x83U); zassert_not_null(in);
  zassert_equal(wl_kernel_usb_bulk_stop(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_false(wl_kernel_usb_bulk_stopped(&adapter));
  zassert_equal(wl_kernel_usb_bulk_deinit(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_equal(wl_packet_tx_submit(&port, tx, sizeof(tx), &event), WL_ERR_INVALID_STATE);
  zassert_ok(c->api->request(c, in, -ECONNABORTED));
  zassert_ok(c->api->request(c, out, -ECONNABORTED));
  zassert_true(wl_kernel_usb_bulk_stopped(&adapter));
  zassert_false(wl_packet_quiescent(&port), "owner still owes completion consumption");
  zassert_equal(wl_packet_tx_take(&port, &event), WL_PACKET_TX_ERROR | WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  zassert_true(wl_packet_quiescent(&port));
  zassert_ok(wl_kernel_usb_bulk_deinit(&adapter));
  init(&port, &adapter, &rx[0][0], 18U);
  zassert_equal(wl_packet_tx_publish(&port, token, WL_PACKET_DRAINED), WL_ERR_NOT_FOUND);
  zassert_ok(wl_kernel_usb_bulk_stop(&adapter));
  zassert_ok(wl_kernel_usb_bulk_deinit(&adapter));
  zassert_true(wakes > 0U);
}

ZTEST_SUITE(static_usb_bulk, NULL, NULL, NULL, NULL, NULL);
