/* SPDX-License-Identifier: Apache-2.0 */
#include "fixture.h"
#include "wirelink/zephyr/node.h"

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

static void init(wl_node_t *node, wl_zephyr_usb_bulk_t *adapter,
                  uint8_t *rx, uint32_t instance) {
  disable_calls = enable_calls = 0U;
  wl_node_layout_t layout = {.instance = instance, .rx_frame_max = 1036, .rx_wire_max = 1036,
      .tx_frame_max = 1036, .tx_wire_max = 1036, .has_tx = 1};
  wl_zephyr_usb_bulk_config_t usb = {.rx = {rx, 4096, 2048}, .wake_consumer = wake};
  zassert_ok(wl_node_bind_transport(node, &layout, wl_zephyr_usb_bulk_transport(adapter, &usb)));
}

static struct net_buf *complete(struct usbd_class_data *c,
                                wl_zephyr_usb_bulk_t *adapter,
                                struct net_buf *buffer, size_t length, int error) {
  net_buf_add(buffer, length);
  unsigned before = wakes;
  zassert_ok(c->api->request(c, buffer, error));
  zassert_equal(wakes, before + 1U, "RX completion must wake the owner once");
  (void)adapter;
  return take_queued(0x02U);
}

ZTEST(static_usb_bulk, test_real_class_ownership_and_recovery) {
  static wl_node_t node;
  static wl_zephyr_usb_bulk_t adapter;
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
  init(&node, &adapter, &rx[0][0], 17U);
  zassert_ok(c->api->init(c));
  c->api->enable(c);
  zassert_is_null(take_queued(0x02U), "enable must only publish work to owner");
  zassert_true(wl_zephyr_usb_bulk_ready(&adapter));
  complete_out_inline = true;
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  zassert_false(wl_zephyr_usb_bulk_ready(&adapter), "normal inline completion rearms directly");
  struct net_buf *out = take_queued(0x02U);
  zassert_not_null(out);
  zassert_equal(net_buf_tailroom(out), 2048U);
  memset(out->data, 0x5a, 64U);
  out = complete(c, &adapter, out, 64U, 0);
  zassert_not_null(out);
  zassert_false(wl_zephyr_usb_bulk_ready(&adapter));
  memset(out->data, 0xa5, 24U);
  out = complete(c, &adapter, out, 24U, 0);
  zassert_is_null(out);
  wl_zephyr_usb_bulk_stats_t st = {0};
  wl_zephyr_usb_bulk_get_stats(&adapter, &st);
  zassert_equal(st.rx_pauses, 1U);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&node.port, &borrow));
  zassert_equal(borrow.data[0], 0x5aU);
  zassert_false(wl_zephyr_usb_bulk_ready(&adapter), "borrowed/full RX must sleep");
  zassert_equal(wl_zephyr_usb_bulk_service(&adapter), WL_ERR_WOULD_BLOCK);
  unsigned before_release = wakes;
  zassert_ok(wl_packet_rx_release(&node.port, &borrow));
  zassert_true(wakes > before_release, "preserve the existing capacity notification");
  zassert_true(wl_zephyr_usb_bulk_ready(&adapter));
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  out = take_queued(0x02U); zassert_not_null(out);
  zassert_ok(wl_packet_rx_borrow(&node.port, &borrow));
  zassert_equal(borrow.data[0], 0xa5U);
  zassert_ok(wl_packet_rx_release(&node.port, &borrow));

  wl_packet_token_t token, event;
  zassert_ok(wl_packet_tx_submit(&node.port, tx, sizeof(tx), &token));
  struct net_buf *in = take_queued(0x83U); zassert_not_null(in);
  zassert_true(udc_get_buf_info(in)->zlp);
  zassert_equal(in->data, tx, "USB must borrow the final frame, not copy it");
  unsigned before_tx = wakes;
  zassert_ok(c->api->request(c, in, 0));
  zassert_equal(wakes, before_tx + 1U, "TX completion must wake the owner once");
  zassert_true(wl_packet_tx_ready(&node.port));
  zassert_equal(wl_packet_tx_take(&node.port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  zassert_equal(event.instance, token.instance);
  zassert_equal(event.sequence, token.sequence);

  complete_in_inline = true;
  before_tx = wakes;
  zassert_ok(wl_packet_tx_submit(&node.port, tx, 24U, &token));
  zassert_equal(wakes, before_tx + 1U, "inline completion must wake the owner once");
  zassert_equal(wl_packet_tx_take(&node.port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  zassert_equal(event.sequence, token.sequence);

  /* A matching endpoint with an old IO token cannot end a new transfer. */
  wl_packet_token_t stale_token = token;
  zassert_ok(wl_packet_tx_submit(&node.port, tx, 24U, &token));
  in = take_queued(0x83U); zassert_not_null(in);
  zassert_false(udc_get_buf_info(in)->zlp);
  struct net_buf *stale = net_buf_alloc_with_data(&stale_pool, stale_data, 1U, K_NO_WAIT);
  zassert_not_null(stale);
  udc_get_buf_info(stale)->ep = 0x83U;
  memcpy((uint8_t *)net_buf_user_data(stale) + sizeof(struct udc_buf_info),
         &stale_token, sizeof(stale_token));
  zassert_equal(c->api->request(c, stale, 0), -EINVAL);
  zassert_true(wl_packet_tx_active(&node.port));
  zassert_false(wl_packet_tx_ready(&node.port));
  zassert_ok(c->api->request(c, in, 0));
  zassert_equal(wl_packet_tx_take(&node.port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);

  /* Reconfigure before the old OUT cancellation arrives. */
  c->api->disable(c);
  context.status.speed = USBD_SPEED_HS;
  c->api->enable(c);
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  zassert_ok(c->api->request(c, out, -ECONNABORTED));
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  out = take_queued(0x04U); zassert_not_null(out);
  zassert_equal(net_buf_tailroom(out), 2048U);
  zassert_ok(wl_packet_tx_submit(&node.port, tx, sizeof(tx), &token));
  in = take_queued(0x85U); zassert_not_null(in);
  zassert_false(udc_get_buf_info(in)->zlp, "HS must use its assigned MPS");
  zassert_ok(c->api->request(c, in, 0));
  zassert_equal(wl_packet_tx_take(&node.port, &event), WL_PACKET_CONSUMED | WL_PACKET_DRAINED);

  /* Full read loses the terminating boundary and requires real reconfigure. */
  net_buf_add(out, 2048U);
  zassert_ok(c->api->request(c, out, 0));
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), WL_ZEPHYR_USB_RX_FAULT_BOUNDARY_LOST);
  zassert_equal(wl_zephyr_usb_bulk_service(&adapter), WL_ERR_IO);
  zassert_false(wl_packet_rx_ready(&node.port));
  zassert_is_null(take_queued(0x04U));
  zassert_ok(wl_zephyr_usb_bulk_recover(&adapter));
  zassert_equal(disable_calls, 1U); zassert_equal(enable_calls, 1U);
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), WL_ZEPHYR_USB_RX_FAULT_BOUNDARY_LOST);
  context.status.speed = USBD_SPEED_FS;
  c->api->enable(c);
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), WL_ZEPHYR_USB_RX_FAULT_NONE);
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  out = take_queued(0x02U); zassert_not_null(out);

  /* stop is asynchronous: preserve both claims until cancellation returns. */
  zassert_ok(wl_packet_tx_submit(&node.port, tx, sizeof(tx), &token));
  in = take_queued(0x83U); zassert_not_null(in);
  zassert_equal(wl_zephyr_usb_bulk_stop(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_false(wl_zephyr_usb_bulk_stopped(&adapter));
  zassert_equal(wl_zephyr_usb_bulk_deinit(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_equal(wl_packet_tx_submit(&node.port, tx, sizeof(tx), &event), WL_ERR_INVALID_STATE);
  zassert_ok(c->api->request(c, in, -ECONNABORTED));
  zassert_ok(c->api->request(c, out, -ECONNABORTED));
  zassert_true(wl_zephyr_usb_bulk_stopped(&adapter));
  zassert_false(wl_packet_quiescent(&node.port), "owner still owes completion consumption");
  zassert_equal(wl_packet_tx_take(&node.port, &event), WL_PACKET_TX_ERROR | WL_PACKET_CONSUMED | WL_PACKET_DRAINED);
  zassert_true(wl_packet_quiescent(&node.port));
  zassert_ok(wl_zephyr_usb_bulk_deinit(&adapter));
  init(&node, &adapter, &rx[0][0], 18U);
  zassert_equal(wl_packet_tx_publish(&node.port, token, WL_PACKET_DRAINED), WL_ERR_NOT_FOUND);
  zassert_ok(wl_zephyr_usb_bulk_stop(&adapter));
  zassert_ok(wl_zephyr_usb_bulk_deinit(&adapter));
  zassert_true(wakes > 0U);
}

/* These hooks model owner preemption at real class/net_buf/stats boundaries. */
static wl_node_t event_node;
static wl_zephyr_usb_bulk_t event_adapter;
static uint8_t event_rx[2][2048];
static struct usbd_context event_context;
static unsigned cycle_calls;
static bool final_deinit;

static struct usbd_class_data *event_begin(uint32_t instance) {
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
  init(&event_node, &event_adapter, &event_rx[0][0], instance);
  unsigned before = wakes;
  c->api->enable(c);
  zassert_true(wakes > before, "enable must signal the owner");
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT));
  zassert_true(wl_zephyr_usb_bulk_ready(&event_adapter));
  zassert_ok(wl_zephyr_usb_bulk_service(&event_adapter));
  return c;
}

static void release_during_callback(void) {
  zassert_true(atomic_get(&event_adapter.callbacks_active) > 0);
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&event_node.port, &borrow));
  zassert_true(wl_packet_take_activity(&event_node.port));
  zassert_not_equal(k_sem_take(&owner_wake, K_NO_WAIT), 0,
                    "data is visible but the callback has not retired");
  unsigned before = wakes;
  zassert_ok(wl_packet_rx_release(&event_node.port, &borrow));
  /* Only one of the two slots was occupied: no producer was blocked on capacity. */
  zassert_false(wl_packet_take_activity(&event_node.port));
  zassert_equal(wakes, before);
  zassert_false(wl_zephyr_usb_bulk_ready(&event_adapter), "RX_REARM not published yet");
}

static void event_end(struct usbd_class_data *c) {
  after_buffer_free = NULL;
  zassert_equal(wl_zephyr_usb_bulk_stop(&event_adapter), WL_ERR_WOULD_BLOCK);
  struct net_buf *out = take_queued(0x02U);
  zassert_not_null(out);
  zassert_ok(c->api->request(c, out, -ECONNABORTED));
  zassert_true(wl_zephyr_usb_bulk_stopped(&event_adapter));
  zassert_true(wl_packet_quiescent(&event_node.port));
  zassert_ok(wl_zephyr_usb_bulk_deinit(&event_adapter));
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
  zassert_equal(wakes, before + 1U);
  zassert_true(wl_packet_activity_pending(&event_node.port));
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT));
  zassert_false(wl_zephyr_usb_bulk_ready(&event_adapter));
  zassert_ok(wl_zephyr_usb_bulk_service(&event_adapter));
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
  zassert_false(wl_packet_rx_ready(&event_node.port));
  zassert_false(wl_zephyr_usb_bulk_ready(&event_adapter));
  k_sem_give(&queried);
  if (publish_before_wait) zassert_ok(k_sem_take(&enter_wait, K_FOREVER));
  for (;;) {
    zassert_ok(k_sem_take(&owner_wake, K_FOREVER));
    if (wl_packet_rx_ready(&event_node.port) && !wl_zephyr_usb_bulk_ready(&event_adapter) &&
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
    zassert_ok(wl_packet_rx_borrow(&event_node.port, &borrow));
    before = wakes;
    zassert_ok(wl_packet_rx_release(&event_node.port, &borrow));
    zassert_equal(wakes, before, "non-full release must not wake the owner");
    zassert_false(wl_zephyr_usb_bulk_ready(&event_adapter));
    zassert_ok(wl_zephyr_usb_bulk_service(&event_adapter));
    event_end(c);
  }
}

static void deinit_on_last_wake(void) {
  if (atomic_get(&event_adapter.callbacks_active) != 0) {
    zassert_false(wl_zephyr_usb_bulk_stopped(&event_adapter));
    return;
  }
  zassert_true(wl_zephyr_usb_bulk_stopped(&event_adapter));
  zassert_true(wl_packet_quiescent(&event_node.port));
  zassert_ok(wl_zephyr_usb_bulk_deinit(&event_adapter));
  final_deinit = true;
  /* No callback code may access adapter after this final outer notification. */
  memset(&event_adapter, 0xa5, sizeof(event_adapter));
}

static uint32_t stop_at_final_stats(void *user) {
  ARG_UNUSED(user);
  if (++cycle_calls == 2U) {
    wl_packet_token_t ev;
    (void)wl_packet_tx_take(&event_node.port, &ev);
    zassert_equal(wl_zephyr_usb_bulk_stop(&event_adapter), WL_ERR_WOULD_BLOCK);
    zassert_false(wl_zephyr_usb_bulk_stopped(&event_adapter));
    zassert_equal(wl_zephyr_usb_bulk_deinit(&event_adapter), WL_ERR_WOULD_BLOCK);
    zassert_not_equal(k_sem_take(&owner_wake, K_NO_WAIT), 0,
                      "stop and disable defer their wake until stats are recorded");
    zassert_false(final_deinit);
  }
  return cycle_calls;
}

ZTEST(static_usb_bulk, test_last_callback_wakes_after_stats_and_silence) {
  static uint8_t tx_buf[24];
  struct usbd_class_data *c = bulk_class();
  c->uds_ctx = &event_context;
  event_context.status.speed = USBD_SPEED_FS;
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, false), 0x02U, 64U);
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, true), 0x83U, 64U);
  after_buffer_free = NULL;
  on_wake = NULL;
  complete_out_inline = false;
  complete_in_inline = false;
  event_adapter.cycle_counter = NULL;
  k_sem_reset(&owner_wake);
  init(&event_node, &event_adapter, &event_rx[0][0], 104U);
  c->api->enable(c);
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT));
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&event_node.port, tx_buf, sizeof(tx_buf), &token));
  struct net_buf *in = take_queued(0x83U);
  zassert_not_null(in);
  cycle_calls = 0U; final_deinit = false;
  event_adapter.cycle_counter = stop_at_final_stats;
  on_wake = deinit_on_last_wake;
  zassert_ok(c->api->request(c, in, 0));
  after_buffer_free = NULL; on_wake = NULL;
  zassert_true(final_deinit);
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT), "last wake must signal physical stop");
  init(&event_node, &event_adapter, &event_rx[0][0], 105U);
  zassert_ok(wl_zephyr_usb_bulk_stop(&event_adapter));
  zassert_ok(wl_zephyr_usb_bulk_deinit(&event_adapter));
}

ZTEST(static_usb_bulk, test_lifecycle_last_port_notify_allows_deinit) {
  struct usbd_class_data *c = bulk_class();
  c->uds_ctx = &event_context;
  event_context.status.speed = USBD_SPEED_FS;
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, false), 0x02U, 64U);
  configure_descriptor(bulk_descriptor(c, USBD_SPEED_FS, true), 0x83U, 64U);
  after_buffer_free = NULL;
  on_wake = NULL;
  complete_out_inline = false;
  complete_in_inline = false;
  event_adapter.cycle_counter = NULL;
  k_sem_reset(&owner_wake);
  init(&event_node, &event_adapter, &event_rx[0][0], 107U);
  c->api->enable(c);
  zassert_ok(k_sem_take(&owner_wake, K_NO_WAIT));
  disable_result = -EIO;
  zassert_equal(wl_node_close(&event_node, 0), WL_ERR_IO);
  zassert_false(wl_node_quiescent(&event_node));
  zassert_false(wl_zephyr_usb_bulk_stopped(&event_adapter));
  disable_result = 0;
  final_deinit = false;
  on_wake = deinit_on_last_wake;
  /* Model the delayed USBD disable after owner stop has returned. Its port
   * notifier can let the waiting owner deinit before disable returns. */
  c->api->disable(c);
  on_wake = NULL;
  zassert_true(final_deinit);
}

enum { WIRE_MAX = 512, USB_UNIT = WL_ZEPHYR_USB_BULK_RX_UNIT(WIRE_MAX) };
BUILD_ASSERT(USB_UNIT == 1024 && WL_ZEPHYR_USB_BULK_RX_UNIT(511) == 512);
static wl_node_t poll_node;
static unsigned received;
static wl_err_t receive_message(wl_endpoint_t *ep, const wl_wire_frame_t *frame) {
  ARG_UNUSED(ep);
  ++received;
  zassert_equal(frame->message_id, received);
  return WL_OK;
}

static void publish_message(struct usbd_class_data *c, uint8_t ep,
                            uint16_t id, uint8_t *expected_storage) {
  struct net_buf *out = take_queued(ep);
  zassert_not_null(out);
  zassert_equal(out->data, expected_storage, "OUT must use the node's own slot");
  zassert_equal(net_buf_tailroom(out), USB_UNIT);
  zassert_ok(wl_wire_native_none_seal(out->data, USB_UNIT, id, 0));
  net_buf_add(out, WL_WIRE_NATIVE_HEADER);
  zassert_ok(c->api->request(c, out, 0));
}

enum wait_operation { NODE_WAIT, NODE_POLL };
static void wait_on_node(void *op, void *b, void *d) {
  ARG_UNUSED(b); ARG_UNUSED(d);
  k_sem_give(&queried);
  if ((uintptr_t)op == NODE_POLL)
    zassert_ok(wl_node_poll(&poll_node, wl_zephyr_clock(), UINT32_MAX));
  else
    zassert_ok(wl_node_wait(&poll_node, k_uptime_get_32(), UINT32_MAX));
}

static void start_node_wait(enum wait_operation op) {
  zassert_false(wl_node_readiness(&poll_node, k_uptime_get_32()).runnable);
  k_thread_create(&wait_thread, wait_stack, K_THREAD_STACK_SIZEOF(wait_stack),
                  wait_on_node, (void *)(uintptr_t)op, NULL, NULL,
                  K_PRIO_COOP(0), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&queried, K_SECONDS(1)));
  zassert_not_equal(k_thread_join(&wait_thread, K_NO_WAIT), 0,
                    "idle owner must actually block until publication");
}

ZTEST(static_usb_bulk, test_standalone_early_enable_and_node_poll) {
  static wl_zephyr_usb_bulk_t adapter;
  static uint8_t rx[2][USB_UNIT] __aligned(WL_NODE_BUFFER_ALIGN);
  wl_node_layout_t layout = {.instance = 201, .rx_frame_max = WIRE_MAX, .rx_wire_max = WIRE_MAX,
      .rx_storage = rx[0], .rx_storage_size = sizeof(rx), .rx_unit_size = USB_UNIT,
      .route = receive_message};
  wl_zephyr_usb_bulk_config_t config = {0}; /* no storage override or user wake */
  struct usbd_class_data *c = bulk_class();
  static struct usbd_context *device_ctx;
  c->uds_ctx = device_ctx; /* Other cases use a synthetic context. */
  use_real_controller = true;
  zassert_ok(wl_zephyr_usb_bulk_device_enable());
  device_ctx = c->uds_ctx;
  zassert_true(device_ctx->status.initialized && device_ctx->status.enabled);
  zassert_ok(wl_zephyr_usb_bulk_device_enable(), "already enabled");
  /* Model SET_CONFIGURATION on the initialized stack before binding a node. */
  c->api->enable(c);
  const enum usbd_speed speed = usbd_bus_speed(c->uds_ctx);
  const uint8_t ep = bulk_descriptor(c, speed, false)->bEndpointAddress;
  zassert_equal(wl_zephyr_usb_bulk_wait(&adapter, K_NO_WAIT), WL_ERR_INVALID_ARG);
  /* A partial override cannot borrow the missing fields from the layout. */
  config.rx.rx_storage_size = sizeof(rx);
  zassert_equal(wl_node_bind_transport(&poll_node, &layout,
                wl_zephyr_usb_bulk_transport(&adapter, &config)), WL_ERR_INVALID_ARG);
  config.rx.rx_storage_size = 0U;
  layout.rx_unit_size = WIRE_MAX + 1U;
  zassert_equal(wl_node_bind_transport(&poll_node, &layout,
                wl_zephyr_usb_bulk_transport(&adapter, &config)), WL_ERR_INVALID_ARG);
  layout.rx_unit_size = USB_UNIT;
  zassert_ok(wl_node_bind_transport(&poll_node, &layout,
                                    wl_zephyr_usb_bulk_transport(&adapter, &config)));
  zassert_true(wl_zephyr_usb_bulk_ready(&adapter));
  zassert_ok(wl_zephyr_usb_bulk_wait(&adapter, K_NO_WAIT), "early enable is latched");
  zassert_ok(wl_node_poll(&poll_node, wl_zephyr_clock(), 0));
  zassert_equal(wl_zephyr_usb_bulk_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);

  received = 0;
  start_node_wait(NODE_WAIT);
  publish_message(c, ep, 1, rx[0]);
  zassert_ok(k_thread_join(&wait_thread, K_SECONDS(1)));
  publish_message(c, ep, 2, rx[1]);
  zassert_is_null(take_queued(ep), "full ring must pause OUT");
  zassert_ok(wl_node_poll(&poll_node, wl_zephyr_clock(), UINT32_MAX));
  zassert_equal(received, 2U);
  wl_zephyr_usb_bulk_stats_t stats;
  wl_zephyr_usb_bulk_get_stats(&adapter, &stats);
  zassert_true(stats.rx_active, "freeing a full ring must rearm OUT in the same step");

  /* No explicit service: the step that freed capacity must already have
   * queued the next read. Its completion wakes a sleeping poll as well. */
  start_node_wait(NODE_POLL);
  publish_message(c, ep, 3, rx[0]);
  zassert_ok(k_thread_join(&wait_thread, K_SECONDS(1)));
  zassert_equal(received, 3U);

  c->api->disable(c);
  zassert_ok(wl_zephyr_usb_bulk_device_disable());
  zassert_ok(wl_zephyr_usb_bulk_device_disable(), "already disabled");
  zassert_equal(wl_node_close(&poll_node, 0), WL_ERR_WOULD_BLOCK);
  zassert_equal(wl_zephyr_usb_bulk_wait(&adapter, K_NO_WAIT), WL_ERR_CANCELLED);
  struct net_buf *out = take_queued(ep);
  zassert_not_null(out);
  zassert_ok(c->api->request(c, out, -ECONNABORTED));
  zassert_ok(wl_node_close(&poll_node, 0));
  zassert_true(wl_node_quiescent(&poll_node));
  zassert_equal(wl_zephyr_usb_bulk_wait(&adapter, K_NO_WAIT), WL_ERR_CANCELLED);
  use_real_controller = false;
}

ZTEST_SUITE(static_usb_bulk, NULL, NULL, NULL, NULL, NULL);
