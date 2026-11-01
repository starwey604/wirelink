/* SPDX-License-Identifier: Apache-2.0 */
#include "fixture.h"

NET_BUF_POOL_FIXED_DEFINE(stale_pool, 1, 0,
    sizeof(struct udc_buf_info) + sizeof(wl_packet_token_t), NULL);
static uint8_t stale_data[1];
static unsigned wakes;
K_SEM_DEFINE(owner_wake, 0, 1);
static void (*on_wake)(void);
static void wake(void *user) {
  ARG_UNUSED(user);
  ++wakes;
  k_sem_give(&owner_wake);
  if (on_wake != NULL) on_wake();
}

static void init(wl_packet_port_t *port, wl_kernel_usb_bulk_t *adapter,
                  uint8_t *rx, uint64_t instance) {
  disable_calls = enable_calls = 0U;
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
  unsigned before = wakes;
  zassert_ok(c->api->request(c, buffer, error));
  zassert_true(wakes > before, "RX completion must signal the owner");
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
  unsigned before_release = wakes;
  zassert_ok(wl_packet_rx_release(&port, &borrow));
  zassert_true(wakes > before_release, "preserve the existing capacity notification");
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
  unsigned before_tx = wakes;
  zassert_ok(c->api->request(c, in, 0));
  zassert_true(wakes > before_tx, "TX completion must signal the owner");
  zassert_true(wl_packet_tx_ready(&port));
  zassert_equal(wl_packet_tx_take(&port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  zassert_equal(event.instance, token.instance);
  zassert_equal(event.sequence, token.sequence);

  complete_in_inline = true;
  before_tx = wakes;
  zassert_ok(wl_packet_tx_submit(&port, tx, 24U, &token));
  zassert_true(wakes > before_tx, "inline completion must signal the owner");
  zassert_equal(wl_packet_tx_take(&port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
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

/* These hooks model owner preemption at real class/net_buf/stats boundaries. */
static wl_packet_port_t event_port;
static wl_kernel_usb_bulk_t event_adapter;
static uint8_t event_rx[2][2048];
static struct usbd_context event_context;
static unsigned cycle_calls;
static bool final_deinit;

static struct usbd_class_data *event_begin(uint64_t instance) {
  struct usbd_class_data *c = bulk_class();
  c->uds_ctx = &event_context;
  event_context.status.speed = USBD_SPEED_FS;
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, false), 0x02U, 64U);
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, true), 0x83U, 64U);
  after_buffer_free = NULL;
  on_wake = NULL;
  complete_out_inline = false;
  complete_in_inline = false;
  k_sem_reset(&owner_wake);
  init(&event_port, &event_adapter, &event_rx[0][0], instance);
  unsigned before = wakes;
  c->api->enable(c);
  zassert_true(wakes > before, "enable must signal the owner");
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT));
  zassert_true(wl_kernel_usb_bulk_ready(&event_adapter));
  zassert_ok(wl_kernel_usb_bulk_service(&event_adapter));
  return c;
}

static void release_during_callback(void) {
  zassert_true(atomic_get(&event_adapter.callbacks_active) > 0);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&event_port, &borrow));
  zassert_true(wl_packet_take_activity(&event_port));
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT), "consume the prompt data wake");
  unsigned before = wakes;
  zassert_ok(wl_packet_rx_release(&event_port, &borrow));
  zassert_true(wl_packet_take_activity(&event_port));
  zassert_true(wakes > before);
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT), "consume the capacity notification");
  zassert_false(wl_kernel_usb_bulk_ready(&event_adapter), "RX_REARM not published yet");
  zassert_not_equal(k_sem_take(&owner_wake, K_NO_WAIT), 0, "owner consumed the currently published notifications");
}

static void event_end(struct usbd_class_data *c) {
  after_buffer_free = NULL;
  zassert_equal(wl_kernel_usb_bulk_stop(&event_adapter), WL_ERR_WOULD_BLOCK);
  struct net_buf *out = take_queued(0x02U);
  zassert_not_null(out);
  zassert_ok(c->api->request(c, out, -ECONNABORTED));
  zassert_true(wl_kernel_usb_bulk_stopped(&event_adapter));
  zassert_true(wl_packet_quiescent(&event_port));
  zassert_ok(wl_kernel_usb_bulk_deinit(&event_adapter));
}

ZTEST(static_usb_bulk, test_release_before_rearm_keeps_final_wake) {
  struct usbd_class_data *c = event_begin(101U);
  struct net_buf *out = take_queued(0x02U);
  zassert_not_null(out);
  net_buf_add(out, 24U);
  after_buffer_free = release_during_callback;
  unsigned before = wakes;
  zassert_ok(c->api->request(c, out, 0));
  after_buffer_free = NULL;
  zassert_true(wakes > before);
  zassert_true(wl_packet_activity_pending(&event_port));
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT));
  zassert_true(wl_kernel_usb_bulk_ready(&event_adapter));
  zassert_ok(wl_kernel_usb_bulk_service(&event_adapter));
  zassert_not_equal(k_sem_take(&owner_wake, K_NO_WAIT), 0, "service must not self-wake");
  event_end(c);
}

K_THREAD_STACK_DEFINE(wait_stack, 2048);
static struct k_thread wait_thread;
K_SEM_DEFINE(queried, 0, 1);
K_SEM_DEFINE(enter_wait, 0, 1);
static bool publish_before_wait;
static void wait_for_one_publication(void *a, void *b, void *d) {
  ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(d);
  zassert_false(wl_packet_rx_ready(&event_port));
  zassert_false(wl_kernel_usb_bulk_ready(&event_adapter));
  k_sem_give(&queried);
  if (publish_before_wait) zassert_ok(k_sem_take(&enter_wait, K_FOREVER));
  for (;;) {
    zassert_ok(k_sem_take(&owner_wake, K_FOREVER));
    if (wl_packet_rx_ready(&event_port) && wl_kernel_usb_bulk_ready(&event_adapter) &&
        atomic_get(&event_adapter.callbacks_active) == 0) break;
  }
}

static void yield_partial_publication(void) { k_yield(); }

ZTEST(static_usb_bulk, test_publication_before_or_after_block) {
  for (unsigned early = 0U; early < 2U; ++early) {
    struct usbd_class_data *c = event_begin(102U + early);
    struct net_buf *out = take_queued(0x02U);
    zassert_not_null(out);
    publish_before_wait = early != 0U;
    /* A higher-priority cooperative waiter runs from queried to K_FOREVER
     * without main preempting it. No sleep/timer is used to manufacture wake. */
    k_thread_create(&wait_thread, wait_stack, K_THREAD_STACK_SIZEOF(wait_stack),
        wait_for_one_publication, NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&queried, K_FOREVER));
    net_buf_add(out, 24U);
    after_buffer_free = yield_partial_publication;
    unsigned before = wakes;
    zassert_ok(c->api->request(c, out, 0));
    after_buffer_free = NULL;
    zassert_true(wakes > before);
    if (publish_before_wait) k_sem_give(&enter_wait);
    zassert_ok(k_thread_join(&wait_thread, K_SECONDS(1)), "data/resource publications must end the wait without polling");
    wl_packet_borrow_t borrow;
    zassert_ok(wl_packet_rx_borrow(&event_port, &borrow));
    before = wakes;
    zassert_ok(wl_packet_rx_release(&event_port, &borrow));
    zassert_true(wakes > before);
    zassert_ok(wl_kernel_usb_bulk_service(&event_adapter));
    event_end(c);
  }
}

static void deinit_on_last_wake(void) {
  if (atomic_get(&event_adapter.callbacks_active) != 0) {
    zassert_false(wl_kernel_usb_bulk_stopped(&event_adapter));
    return;
  }
  zassert_true(wl_kernel_usb_bulk_stopped(&event_adapter));
  zassert_true(wl_packet_quiescent(&event_port));
  zassert_ok(wl_kernel_usb_bulk_deinit(&event_adapter));
  final_deinit = true;
  /* No callback code may access adapter after this final outer notification. */
  memset(&event_adapter, 0xa5, sizeof(event_adapter));
}

static uint32_t stop_at_final_stats(void *user) {
  ARG_UNUSED(user);
  if (++cycle_calls == 2U) {
    zassert_equal(wl_kernel_usb_bulk_stop(&event_adapter), WL_ERR_WOULD_BLOCK);
    zassert_false(wl_kernel_usb_bulk_stopped(&event_adapter));
    zassert_equal(wl_kernel_usb_bulk_deinit(&event_adapter), WL_ERR_WOULD_BLOCK);
    /* Consume disable's wake while callback stats still use the adapter. */
    zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT));
    zassert_false(final_deinit);
  }
  return cycle_calls;
}

ZTEST(static_usb_bulk, test_last_callback_wakes_after_stats_and_silence) {
  struct usbd_class_data *c = event_begin(104U);
  struct net_buf *out = take_queued(0x02U);
  zassert_not_null(out);
  cycle_calls = 0U; final_deinit = false;
  event_adapter.cycle_counter = stop_at_final_stats;
  after_buffer_free = release_during_callback;
  on_wake = deinit_on_last_wake;
  net_buf_add(out, 24U);
  zassert_ok(c->api->request(c, out, 0));
  after_buffer_free = NULL; on_wake = NULL;
  zassert_true(final_deinit);
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT), "last wake must survive stop's earlier wake");
  init(&event_port, &event_adapter, &event_rx[0][0], 105U);
  zassert_ok(wl_kernel_usb_bulk_stop(&event_adapter));
  zassert_ok(wl_kernel_usb_bulk_deinit(&event_adapter));
}

ZTEST(static_usb_bulk, test_lifecycle_last_port_notify_allows_deinit) {
  struct usbd_class_data *c = event_begin(107U);
  struct net_buf *out = take_queued(0x02U);
  zassert_not_null(out);
  /* Empty completion leaves no data borrow or IO lease; do not rearm. */
  zassert_ok(c->api->request(c, out, 0));
  disable_result = -EIO;
  zassert_equal(wl_kernel_usb_bulk_stop(&event_adapter), WL_ERR_IO);
  zassert_false(wl_kernel_usb_bulk_stopped(&event_adapter));
  disable_result = 0;
  final_deinit = false;
  on_wake = deinit_on_last_wake;
  /* Model the delayed USBD disable after owner stop has returned. Its port
   * notifier can let the waiting owner deinit before disable returns. */
  c->api->disable(c);
  on_wake = NULL;
  zassert_true(final_deinit);
}

ZTEST_SUITE(static_usb_bulk, NULL, NULL, NULL, NULL, NULL);
