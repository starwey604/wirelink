/* SPDX-License-Identifier: Apache-2.0 */
/* K1 isolated port binding, derived from the product usb_bulk adapter.
 * Descriptor/net_buf/ZLP/reconfiguration behavior is retained. This source
 * replaces that adapter in a prototype build; it never links the old core. */

#include "wirelink/zephyr/usb_bulk.h"
#include "wirelink/zephyr/detail/timeout.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>

static const wl_transport_ops_t transport_ops;

#define WL_USB_FS_MPS 64U
#define WL_USB_HS_MPS 512U

enum adapter_flag {
  ADAPTER_INITIALIZED,
  ADAPTER_ENABLED,
  ADAPTER_RX_ACTIVE,
  ADAPTER_RX_REARM,
  ADAPTER_TX_ACTIVE,
  ADAPTER_RX_FAULTED,
  ADAPTER_RX_CANCEL_EXPECTED,
  ADAPTER_RX_FAULT_PENDING,
  ADAPTER_RECOVERY_REQUESTED,
  ADAPTER_RECOVERY_DISABLED,
  ADAPTER_STOPPING,
};

struct bulk_descriptors {
  struct usb_if_descriptor interface;
  struct usb_ep_descriptor fs_out;
  struct usb_ep_descriptor fs_in;
  struct usb_ep_descriptor hs_out;
  struct usb_ep_descriptor hs_in;
  struct usb_desc_header terminator;
};

struct bulk_class_data {
  struct bulk_descriptors *descriptors;
  const struct usb_desc_header **fs_descriptors;
  const struct usb_desc_header **hs_descriptors;
  wl_zephyr_usb_bulk_t *adapter;
  bool early_enabled;
  uint16_t early_in_mps;
  uint8_t early_out_ep, early_in_ep;
};

/* Bind and SET_CONFIGURATION may run on different threads. */
static struct k_spinlock class_lock;

NET_BUF_POOL_FIXED_DEFINE(wl_usb_bulk_pool, 2, 0, sizeof(struct udc_buf_info) + sizeof(wl_packet_token_t),
                          NULL);

static int queue_out(struct usbd_class_data *class_data,
                     wl_zephyr_usb_bulk_t *adapter, bool from_callback);
static struct usbd_class_data *wl_usb_bulk_class_data(void);

static struct usb_ep_descriptor *bulk_out_descriptor(
    struct usbd_class_data *class_data) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);
  struct usbd_context *context = usbd_class_get_ctx(class_data);

  if (USBD_SUPPORTS_HIGH_SPEED &&
      usbd_bus_speed(context) == USBD_SPEED_HS) {
    return &private_data->descriptors->hs_out;
  }
  return &private_data->descriptors->fs_out;
}

static struct usb_ep_descriptor *bulk_in_descriptor(
    struct usbd_class_data *class_data) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);
  struct usbd_context *context = usbd_class_get_ctx(class_data);

  if (USBD_SUPPORTS_HIGH_SPEED &&
      usbd_bus_speed(context) == USBD_SPEED_HS) {
    return &private_data->descriptors->hs_in;
  }
  return &private_data->descriptors->fs_in;
}

void wl_zephyr_usb_bulk_notify(void *context) {
  wl_zephyr_usb_bulk_t *adapter = context;
  /* Port activity is already latched. The last callback delivers its wake. */
  if (atomic_get(&adapter->callbacks_active) == 0) {
    wl_zephyr_usb_bulk_wake_fn wake = adapter->wake_consumer;
    void *user = adapter->wake_user_data;
    k_sem_give(&adapter->wake);
    if (wake != NULL) wake(user);
  }
}

static void wake_consumer(const wl_zephyr_usb_bulk_t *adapter) {
  wl_packet_activity(adapter->port);
}

/* Use memcpy: net_buf user data need not satisfy uint64_t alignment. */
static void set_buffer_token(struct net_buf *buffer, wl_packet_token_t token) {
  uint8_t *user = net_buf_user_data(buffer);
  memcpy(user + sizeof(struct udc_buf_info), &token, sizeof(token));
}
static wl_packet_token_t buffer_token(struct net_buf *buffer) {
  wl_packet_token_t token;
  const uint8_t *user = net_buf_user_data(buffer);
  memcpy(&token, user + sizeof(struct udc_buf_info), sizeof(token));
  return token;
}
static bool token_equal(wl_packet_token_t a, wl_packet_token_t b) {
  return a.instance == b.instance && a.sequence == b.sequence;
}

static void latch_rx_fault(wl_zephyr_usb_bulk_t *adapter,
                           wl_zephyr_usb_rx_fault_t reason) {
  if (atomic_cas(&adapter->rx_fault, WL_ZEPHYR_USB_RX_FAULT_NONE, reason)) {
    atomic_set(&adapter->last_rx_fault, reason);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_FAULT_PENDING);
  }
  atomic_set_bit(&adapter->flags, ADAPTER_RX_FAULTED);
}

static inline void stat_add(atomic_t *value, atomic_val_t delta) {
#if defined(__GNUC__) || defined(__clang__)
  __atomic_store_n(value, __atomic_load_n(value, __ATOMIC_RELAXED) + delta,
                   __ATOMIC_RELAXED);
#else
  atomic_add(value, delta);
#endif
}

static inline void stat_inc(atomic_t *value) {
  stat_add(value, 1);
}

static uint32_t cycle_count(const wl_zephyr_usb_bulk_t *adapter) {
  return adapter->cycle_counter(adapter->cycle_counter_user_data);
}

static void atomic_max(atomic_t *maximum, uint32_t value) {
  atomic_val_t previous = atomic_get(maximum);

  while (value > (uint32_t)previous &&
         !atomic_cas(maximum, previous, (atomic_val_t)value)) {
    previous = atomic_get(maximum);
  }
}

static void record_cycles(const wl_zephyr_usb_bulk_t *adapter, atomic_t *total,
                          atomic_t *maximum, uint32_t started) {
  const uint32_t elapsed = cycle_count(adapter) - started;
  atomic_add(total, (atomic_val_t)elapsed);
  atomic_max(maximum, elapsed);
}

wl_packet_submit_t wl_zephyr_usb_bulk_submit(void *user_data, const uint8_t *data,
                                             size_t length, wl_packet_token_t token) {
  wl_zephyr_usb_bulk_t *adapter = user_data;
  struct usbd_class_data *class_data;
  struct usbd_context *context;
  struct net_buf *buffer;
  struct udc_buf_info *info;
  uint32_t started = 0U;
  int result;

  if (adapter == NULL || data == NULL || length == 0U) return WL_PACKET_FAILED;
  const atomic_val_t flags = atomic_get(&adapter->flags);
  if (!(flags & BIT(ADAPTER_INITIALIZED)) || (flags & BIT(ADAPTER_STOPPING))) {
    return WL_PACKET_FAILED;
  }
  if (!(flags & BIT(ADAPTER_ENABLED))) return WL_PACKET_BUSY;
  if (atomic_test_and_set_bit(&adapter->flags, ADAPTER_TX_ACTIVE)) {
    return WL_PACKET_BUSY;
  }
  if (adapter->cycle_counter != NULL) started = cycle_count(adapter);

  class_data = wl_usb_bulk_class_data();
  context = usbd_class_get_ctx(class_data);
  buffer = net_buf_alloc_with_data(&wl_usb_bulk_pool, (uint8_t *)data, length,
                                   K_NO_WAIT);
  if (buffer == NULL) {
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    if (adapter->cycle_counter != NULL) {
      record_cycles(adapter, &adapter->tx_sink_cycles,
                    &adapter->tx_sink_max_cycles, started);
    }
    return WL_PACKET_BUSY;
  }

  info = udc_get_buf_info(buffer);
  adapter->tx_endpoint = adapter->cached_in_ep;
  info->ep = adapter->tx_endpoint;
  const uint16_t mps = adapter->cached_in_mps;
  const bool need_zlp = (mps & (mps - 1U)) == 0U ? (length & (mps - 1U)) == 0U
                                                  : (length % mps) == 0U;
  if (need_zlp) {
    udc_ep_buf_set_zlp(buffer);
  }

  adapter->tx_token = token;
  set_buffer_token(buffer, token);
  result = usbd_ep_enqueue(class_data, buffer);
  if (result != 0) {
    adapter->tx_token = (wl_packet_token_t){0};
    adapter->tx_endpoint = 0U;
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    usbd_ep_buf_free(context, buffer);
    if (adapter->cycle_counter != NULL) {
      record_cycles(adapter, &adapter->tx_sink_cycles,
                    &adapter->tx_sink_max_cycles, started);
    }
    return result == -ENOMEM || result == -EBUSY ? WL_PACKET_BUSY
                                                  : WL_PACKET_FAILED;
  }
  stat_inc(&adapter->tx_submissions);
  stat_add(&adapter->tx_bytes, (atomic_val_t)length);
  if (adapter->cycle_counter != NULL) {
    record_cycles(adapter, &adapter->tx_sink_cycles,
                  &adapter->tx_sink_max_cycles, started);
  }
  return WL_PACKET_STARTED;
}

static bool rx_enabled(atomic_val_t flags) {
  return (flags & BIT(ADAPTER_ENABLED)) &&
         !(flags & (BIT(ADAPTER_STOPPING) | BIT(ADAPTER_RX_FAULTED)));
}

static bool owner_rearm_pending(atomic_val_t flags) {
  return rx_enabled(flags) && (flags & BIT(ADAPTER_RX_REARM)) &&
         !(flags & BIT(ADAPTER_RX_ACTIVE));
}

/* Return the producer role to service only after the old lease is retired.
 * A concurrent stop, disable or reconfiguration changes whether to retry. */
static void retire_out(wl_zephyr_usb_bulk_t *adapter) {
  adapter->rx_endpoint = 0U;
  atomic_val_t flags;
  atomic_val_t next;
  do {
    flags = atomic_get(&adapter->flags);
    next = flags & ~(BIT(ADAPTER_RX_ACTIVE) | BIT(ADAPTER_RX_REARM));
    if (rx_enabled(flags)) next |= BIT(ADAPTER_RX_REARM);
  } while (!atomic_cas(&adapter->flags, flags, next));
}

static int bulk_request_impl(struct usbd_class_data *class_data,
                        struct net_buf *buffer, int error, wl_zephyr_usb_bulk_t *adapter) {
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct udc_buf_info *info = udc_get_buf_info(buffer);
  const uint8_t endpoint = info->ep;
  uint32_t started = 0U;

  if (adapter == NULL) {
    usbd_ep_buf_free(context, buffer);
    return -ENODEV;
  }
  if (adapter->cycle_counter != NULL) started = cycle_count(adapter);

  if (buffer->user_data_size < sizeof(struct udc_buf_info) + sizeof(wl_packet_token_t)) {
    atomic_inc(&adapter->errors);
    usbd_ep_buf_free(context, buffer);
    return -EINVAL;
  }
  wl_packet_token_t token = buffer_token(buffer);
  const atomic_val_t flags = atomic_get(&adapter->flags);
  if ((flags & BIT(ADAPTER_TX_ACTIVE)) &&
      endpoint == adapter->tx_endpoint && token_equal(token, adapter->tx_token)) {
    usbd_ep_buf_free(context, buffer);
    adapter->tx_token = (wl_packet_token_t){0};
    adapter->tx_endpoint = 0U;
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    stat_inc(&adapter->tx_completions);
    if (error != 0) atomic_inc(&adapter->errors);
    int result = wl_packet_tx_publish(adapter->port, token,
        error == 0 ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR);
    if (result != WL_OK) atomic_inc(&adapter->errors);
    if (adapter->cycle_counter != NULL) {
      record_cycles(adapter, &adapter->tx_callback_cycles,
                    &adapter->tx_callback_max_cycles, started);
    }
    return result;
  }

  if (!(flags & BIT(ADAPTER_RX_ACTIVE)) ||
      endpoint != adapter->rx_endpoint ||
      !token_equal(token, adapter->rx_unit_claim.token)) {
    atomic_inc(&adapter->errors);
    usbd_ep_buf_free(context, buffer);
    return -EINVAL;
  }

  const bool cancel_expected = (flags & BIT(ADAPTER_RX_CANCEL_EXPECTED)) != 0;
  if (cancel_expected) atomic_clear_bit(&adapter->flags, ADAPTER_RX_CANCEL_EXPECTED);
  /* A full request can complete before a terminating short packet/ZLP.
   * After an IO failure its suffix is equally unsafe to treat as a frame.
   * Latch RX stopped until USBD is disabled/reset and enabled again. */
  if (flags & BIT(ADAPTER_STOPPING)) {
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
  } else if (error != 0 || buffer->len >= adapter->rx_unit_claim.span.length) {
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
    if (error != -ECONNABORTED ||
        (!cancel_expected && (flags & BIT(ADAPTER_ENABLED)))) {
      atomic_inc(&adapter->errors);
      latch_rx_fault(adapter, error != 0 ? WL_ZEPHYR_USB_RX_FAULT_TRANSFER
                                         : WL_ZEPHYR_USB_RX_FAULT_BOUNDARY_LOST);
    }
  } else if (buffer->len == 0U || buffer->len > adapter->frame_limit) {
    if (buffer->len > adapter->frame_limit) atomic_inc(&adapter->errors);
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
  } else if (wl_packet_rx_commit(adapter->port, &adapter->rx_unit_claim,
                               buffer->len) != WL_OK) {
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
    atomic_inc(&adapter->errors);
    latch_rx_fault(adapter, WL_ZEPHYR_USB_RX_FAULT_QUEUE);
  } else {
    stat_add(&adapter->rx_bytes, (atomic_val_t)buffer->len);
  }

  usbd_ep_buf_free(context, buffer);
  /* Free may overlap owner stop, and this completion may have latched a
   * fault. Recheck admission here, not against the completed request. */
  const atomic_val_t rearm_flags = atomic_get(&adapter->flags);
  if (rx_enabled(rearm_flags) && atomic_get(&adapter->callbacks_active) == 1) {
    if (rearm_flags & BIT(ADAPTER_RX_REARM)) atomic_clear_bit(&adapter->flags, ADAPTER_RX_REARM);
    (void)queue_out(class_data, adapter, true);
  } else {
    retire_out(adapter);
  }
  stat_inc(&adapter->rx_completions);
  wake_consumer(adapter);
  if (adapter->cycle_counter != NULL) {
    record_cycles(adapter, &adapter->rx_callback_cycles,
                  &adapter->rx_callback_max_cycles, started);
  }
  return 0;
}

/* Keep the binding alive until all callback work is published. Once stopped
 * is observable, the owner may reclaim it in the optional notification. */
static void finish_callback(wl_zephyr_usb_bulk_t *adapter) {
  k_spinlock_key_t key = k_spin_lock(&class_lock);
  wl_zephyr_usb_bulk_wake_fn wake = NULL;
  void *user = adapter->wake_user_data;
  if (atomic_dec(&adapter->callbacks_active) == 1) {
    wake = adapter->wake_consumer;
    k_sem_give(&adapter->wake);
  }
  k_spin_unlock(&class_lock, key);
  if (wake != NULL) wake(user);
}

static int bulk_request(struct usbd_class_data *class_data,
                        struct net_buf *buffer, int error) {
  struct bulk_class_data *data = usbd_class_get_private(class_data);
  k_spinlock_key_t key = k_spin_lock(&class_lock);
  wl_zephyr_usb_bulk_t *adapter = data->adapter;
  if (adapter != NULL) atomic_inc(&adapter->callbacks_active);
  k_spin_unlock(&class_lock, key);
  int result = bulk_request_impl(class_data, buffer, error, adapter);
  if (adapter != NULL) finish_callback(adapter);
  return result;
}

static void apply_enabled(wl_zephyr_usb_bulk_t *adapter, struct bulk_class_data *data) {
  adapter->cached_in_mps = data->early_in_mps;
  adapter->cached_out_ep = data->early_out_ep;
  adapter->cached_in_ep = data->early_in_ep;
  atomic_set(&adapter->rx_fault, WL_ZEPHYR_USB_RX_FAULT_NONE);
  atomic_clear_bit(&adapter->flags, ADAPTER_RX_FAULTED);
  atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
  atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_DISABLED);
  atomic_or(&adapter->flags, BIT(ADAPTER_ENABLED) | BIT(ADAPTER_RX_REARM));
}

static void bulk_enable(struct usbd_class_data *class_data) {
  struct bulk_class_data *data = usbd_class_get_private(class_data);
  const struct usb_ep_descriptor *in = bulk_in_descriptor(class_data);
  const struct usb_ep_descriptor *out = bulk_out_descriptor(class_data);
  k_spinlock_key_t key = k_spin_lock(&class_lock);
  if (data->early_enabled) {
    k_spin_unlock(&class_lock, key);
    return;
  }
  data->early_enabled = true;
  data->early_in_mps = sys_le16_to_cpu(in->wMaxPacketSize);
  data->early_in_ep = in->bEndpointAddress;
  data->early_out_ep = out->bEndpointAddress;
  wl_zephyr_usb_bulk_t *adapter = data->adapter;
  if (adapter != NULL && !atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) {
    atomic_inc(&adapter->callbacks_active);
    apply_enabled(adapter, data);
    k_spin_unlock(&class_lock, key);
    wake_consumer(adapter);
    finish_callback(adapter);
  } else {
    k_spin_unlock(&class_lock, key);
  }
}

static void bulk_disable(struct usbd_class_data *class_data) {
  struct bulk_class_data *data = usbd_class_get_private(class_data);
  k_spinlock_key_t key = k_spin_lock(&class_lock);
  data->early_enabled = false;
  data->early_in_mps = 0U;
  data->early_out_ep = data->early_in_ep = 0U;
  wl_zephyr_usb_bulk_t *adapter = data->adapter;
  if (adapter != NULL) {
    atomic_inc(&adapter->callbacks_active);
    if (atomic_test_bit(&adapter->flags, ADAPTER_RX_ACTIVE))
      atomic_set_bit(&adapter->flags, ADAPTER_RX_CANCEL_EXPECTED);
    atomic_clear_bit(&adapter->flags, ADAPTER_ENABLED);
  }
  k_spin_unlock(&class_lock, key);
  if (adapter != NULL) {
    wake_consumer(adapter);
    finish_callback(adapter);
  }
}

static int bulk_init(struct usbd_class_data *class_data) {
  ARG_UNUSED(class_data);
  return 0;
}

static const void *bulk_get_descriptors(struct usbd_class_data *class_data,
                                       enum usbd_speed speed) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);

  if (USBD_SUPPORTS_HIGH_SPEED && speed == USBD_SPEED_HS) {
    return private_data->hs_descriptors;
  }
  return private_data->fs_descriptors;
}

static struct usbd_class_api bulk_api = {
    .request = bulk_request,
    .get_desc = bulk_get_descriptors,
    .enable = bulk_enable,
    .disable = bulk_disable,
    .init = bulk_init,
};

static struct bulk_descriptors descriptors = {
    .interface =
        {
            .bLength = sizeof(struct usb_if_descriptor),
            .bDescriptorType = USB_DESC_INTERFACE,
            .bInterfaceNumber = 0U,
            .bAlternateSetting = 0U,
            .bNumEndpoints = 2U,
            .bInterfaceClass = USB_BCC_VENDOR,
            .bInterfaceSubClass = WL_ZEPHYR_USB_BULK_INTERFACE_SUBCLASS,
            .bInterfaceProtocol = WL_ZEPHYR_USB_BULK_INTERFACE_PROTOCOL,
            .iInterface = 0U,
        },
    .fs_out =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_ZEPHYR_USB_BULK_OUT_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(WL_USB_FS_MPS),
            .bInterval = 0U,
        },
    .fs_in =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_ZEPHYR_USB_BULK_IN_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(WL_USB_FS_MPS),
            .bInterval = 0U,
        },
    .hs_out =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_ZEPHYR_USB_BULK_OUT_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(WL_USB_HS_MPS),
            .bInterval = 0U,
        },
    .hs_in =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_ZEPHYR_USB_BULK_IN_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(WL_USB_HS_MPS),
            .bInterval = 0U,
        },
    .terminator = {.bLength = 0U, .bDescriptorType = 0U},
};

static const struct usb_desc_header *fs_descriptors[] = {
    (struct usb_desc_header *)&descriptors.interface,
    (struct usb_desc_header *)&descriptors.fs_out,
    (struct usb_desc_header *)&descriptors.fs_in,
    (struct usb_desc_header *)&descriptors.terminator,
};

static const struct usb_desc_header *hs_descriptors[] = {
    (struct usb_desc_header *)&descriptors.interface,
    (struct usb_desc_header *)&descriptors.hs_out,
    (struct usb_desc_header *)&descriptors.hs_in,
    (struct usb_desc_header *)&descriptors.terminator,
};

static struct bulk_class_data private_data = {
    .descriptors = &descriptors,
    .fs_descriptors = fs_descriptors,
    .hs_descriptors = hs_descriptors,
};

USBD_DEFINE_CLASS(wirelink_bulk_0, &bulk_api, &private_data, NULL);

static struct usbd_class_data *wl_usb_bulk_class_data(void) {
  return &wirelink_bulk_0;
}

/* The owner arms idle RX; a completing callback keeps RX_ACTIVE throughout
 * replacement. Nothing after a successful enqueue may change the lease:
 * the new request may already have completed inline. */
static int queue_out(struct usbd_class_data *class_data,
                     wl_zephyr_usb_bulk_t *adapter, bool from_callback) {
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct net_buf *buffer;
  struct udc_buf_info *info;
  wl_packet_claim_t *claim = &adapter->rx_unit_claim;
  int result;

  result = wl_packet_rx_claim(adapter->port, adapter->maximum_rx_size, claim);
  if (result != WL_OK) {
    if (result == WL_ERR_WOULD_BLOCK) stat_inc(&adapter->rx_pauses);
    retire_out(adapter);
    return result;
  }
  buffer = net_buf_alloc_with_data(&wl_usb_bulk_pool,
                                   claim->span.data,
                                   claim->span.length, K_NO_WAIT);
  if (buffer == NULL) {
    (void)wl_packet_rx_abort(adapter->port, claim);
    retire_out(adapter);
    return WL_ERR_WOULD_BLOCK;
  }
  net_buf_reset(buffer);
  info = udc_get_buf_info(buffer);
  adapter->rx_endpoint = adapter->cached_out_ep;
  info->ep = adapter->rx_endpoint;

  set_buffer_token(buffer, claim->token);
  if (!from_callback) {
    atomic_clear_bit(&adapter->flags, ADAPTER_RX_REARM);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
  }
  result = usbd_ep_enqueue(class_data, buffer);
  if (result != 0) {
    (void)wl_packet_rx_abort(adapter->port, claim);
    usbd_ep_buf_free(context, buffer);
    atomic_inc(&adapter->errors);
    retire_out(adapter);
    return WL_ERR_IO;
  }

  stat_inc(&adapter->rx_claims);
  return WL_OK;
}

static wl_err_t initialize(wl_zephyr_usb_bulk_t *adapter, const wl_zephyr_usb_bulk_config_t *config,
                           const wl_transport_attach_t *info) {

  if (info->rx_wire_max < 4U ||
      info->rx_wire_max >= wl_packet_rx_unit_size(info->port)) {
    return WL_ERR_INVALID_ARG;
  }
  k_spinlock_key_t key = k_spin_lock(&class_lock);
  if (private_data.adapter != NULL) {
    k_spin_unlock(&class_lock, key);
    return WL_ERR_INVALID_ARG;
  }
  const wl_zephyr_usb_bulk_config_t value = *config;
  memset(adapter, 0, sizeof(*adapter));
  adapter->transport.ops = &transport_ops;
  adapter->config = value;
  adapter->port = info->port;
  adapter->frame_limit = info->rx_wire_max;
  adapter->maximum_rx_size = wl_packet_rx_unit_size(info->port);
  adapter->wake_user_data = value.wake_user_data;
  adapter->wake_consumer = value.wake_consumer;
  adapter->cycle_counter_user_data = value.cycle_counter_user_data;
  adapter->cycle_counter = value.cycle_counter;
  k_sem_init(&adapter->wake, 0, 1);
  private_data.adapter = adapter;
  atomic_set_bit(&adapter->flags, ADAPTER_INITIALIZED);
  bool enabled = private_data.early_enabled;
  if (enabled) apply_enabled(adapter, &private_data);
  k_spin_unlock(&class_lock, key);
  if (enabled) wake_consumer(adapter);
  return WL_OK;
}

int wl_zephyr_usb_bulk_service(wl_zephyr_usb_bulk_t *adapter) {
  if (adapter == NULL) return WL_ERR_INVALID_ARG;
  if (!atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) return WL_ERR_INVALID_ARG;
  (void)k_sem_take(&adapter->wake, K_NO_WAIT);
  const atomic_val_t flags = atomic_get(&adapter->flags);

  int result = WL_OK;
  if (owner_rearm_pending(flags)) {
    if (!wl_packet_rx_available(adapter->port)) {
      result = WL_ERR_WOULD_BLOCK;
    } else {
      const uint32_t started = adapter->cycle_counter != NULL ? cycle_count(adapter) : 0U;
      result = queue_out(&wirelink_bulk_0, adapter, false);
      stat_inc(&adapter->active_service_calls);
      if (adapter->cycle_counter != NULL) {
        record_cycles(adapter, &adapter->active_service_cycles,
                      &adapter->active_service_max_cycles, started);
      }
    }
  }
  if (flags & BIT(ADAPTER_RX_FAULT_PENDING)) {
    atomic_clear_bit(&adapter->flags, ADAPTER_RX_FAULT_PENDING);
    return WL_ERR_IO;
  }
  return (flags & BIT(ADAPTER_RX_FAULTED)) ? WL_ERR_IO : result;
}

wl_err_t wl_zephyr_usb_bulk_wait(wl_zephyr_usb_bulk_t *adapter, k_timeout_t timeout) {
  if (adapter == NULL) return WL_ERR_INVALID_ARG;
  const atomic_val_t flags = atomic_get(&adapter->flags);
  if (flags & BIT(ADAPTER_STOPPING)) return WL_ERR_CANCELLED;
  if (!(flags & BIT(ADAPTER_INITIALIZED))) return WL_ERR_INVALID_ARG;
  int result = k_sem_take(&adapter->wake, timeout);
  if (atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) return WL_ERR_CANCELLED;
  if (result == -EAGAIN || result == -EBUSY) return WL_ERR_NO_DATA;
  return result == 0 ? WL_OK : WL_ERR_IO;
}

int wl_zephyr_usb_bulk_ready(const wl_zephyr_usb_bulk_t *adapter) {
  if (adapter == NULL) return 0;
  const atomic_val_t flags = atomic_get(&adapter->flags);
  return (flags & BIT(ADAPTER_INITIALIZED)) &&
      ((flags & BIT(ADAPTER_RX_FAULT_PENDING)) ||
       (owner_rearm_pending(flags) && wl_packet_rx_available(adapter->port)));
}

static bool stopped(const wl_zephyr_usb_bulk_t *adapter) {
  const atomic_val_t flags = atomic_get(&adapter->flags);
  return (flags & BIT(ADAPTER_STOPPING)) && atomic_get(&adapter->callbacks_active) == 0 &&
      !(flags & (BIT(ADAPTER_ENABLED) | BIT(ADAPTER_RX_ACTIVE) | BIT(ADAPTER_TX_ACTIVE)));
}

static void detach(wl_zephyr_usb_bulk_t *adapter) {
  private_data.adapter = NULL;
  atomic_clear_bit(&adapter->flags, ADAPTER_INITIALIZED);
}

static wl_err_t stop(wl_zephyr_usb_bulk_t *adapter, bool detach_when_stopped) {
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED))
    return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_INVALID_STATE;
  /* stop and USBD disable both publish activity; defer the outer notification
   * until the status and (for transport close) detachment are final. */
  atomic_inc(&adapter->callbacks_active);
  atomic_set_bit(&adapter->flags, ADAPTER_STOPPING);
  wl_packet_stop(adapter->port);
  wl_err_t result = WL_OK;
  if (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED)) {
    struct usbd_context *context = usbd_class_get_ctx(wl_usb_bulk_class_data());
    if (context == NULL) result = WL_ERR_NOT_INITIALIZED;
    else if (usbd_disable(context) != 0) result = WL_ERR_IO;
  }

  k_spinlock_key_t key = k_spin_lock(&class_lock);
  wl_zephyr_usb_bulk_wake_fn wake = atomic_dec(&adapter->callbacks_active) == 1
      ? adapter->wake_consumer : NULL;
  void *user = adapter->wake_user_data;
  if (result == WL_OK && !stopped(adapter)) result = WL_ERR_WOULD_BLOCK;
  if (result == WL_OK && detach_when_stopped) detach(adapter);
  k_sem_give(&adapter->wake);
  k_spin_unlock(&class_lock, key);
  if (wake != NULL) wake(user);
  return result;
}

int wl_zephyr_usb_bulk_stop(wl_zephyr_usb_bulk_t *adapter) {
  return stop(adapter, false);
}

int wl_zephyr_usb_bulk_stopped(const wl_zephyr_usb_bulk_t *adapter) {
  if (adapter == NULL) return 0;
  k_spinlock_key_t key = k_spin_lock(&class_lock);
  bool result = stopped(adapter);
  k_spin_unlock(&class_lock, key);
  return result;
}

int wl_zephyr_usb_bulk_deinit(wl_zephyr_usb_bulk_t *adapter) {
  k_spinlock_key_t key = k_spin_lock(&class_lock);
  wl_err_t result = WL_OK;
  if (adapter == NULL || private_data.adapter != adapter) result = WL_ERR_INVALID_ARG;
  else if (!stopped(adapter) || !wl_packet_quiescent(adapter->port)) result = WL_ERR_WOULD_BLOCK;
  else detach(adapter);
  k_spin_unlock(&class_lock, key);
  return result;
}

wl_zephyr_usb_rx_fault_t wl_zephyr_usb_bulk_rx_fault(
    const wl_zephyr_usb_bulk_t *adapter) {
  return adapter == NULL ? WL_ZEPHYR_USB_RX_FAULT_NONE
                         : (wl_zephyr_usb_rx_fault_t)atomic_get(&adapter->rx_fault);
}

int wl_zephyr_usb_bulk_recover(wl_zephyr_usb_bulk_t *adapter) {
  int result;
  if (adapter == NULL ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr() || atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) return WL_ERR_INVALID_STATE;
  if (!atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULTED)) return WL_ERR_INVALID_STATE;
  struct usbd_context *context = usbd_class_get_ctx(wl_usb_bulk_class_data());
  if (context == NULL) return WL_ERR_NOT_INITIALIZED;
  if (atomic_test_and_set_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED))
    return WL_ERR_WOULD_BLOCK;
  if (!atomic_test_bit(&adapter->flags, ADAPTER_RECOVERY_DISABLED)) {
    result = usbd_disable(context);
    if (result != 0) {
      atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
      return WL_ERR_IO;
    }
    atomic_set_bit(&adapter->flags, ADAPTER_RECOVERY_DISABLED);
  }
  result = usbd_enable(context);
  if (result != 0) atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
  return result == 0 ? WL_OK : WL_ERR_IO;
}

void wl_zephyr_usb_bulk_reset_stats(wl_zephyr_usb_bulk_t *adapter) {
  if (adapter == NULL) {
    return;
  }

  atomic_set(&adapter->rx_claims, 0);
  atomic_set(&adapter->rx_completions, 0);
  atomic_set(&adapter->rx_bytes, 0);
  atomic_set(&adapter->rx_pauses, 0);
  atomic_set(&adapter->tx_submissions, 0);
  atomic_set(&adapter->tx_completions, 0);
  atomic_set(&adapter->tx_bytes, 0);
  atomic_set(&adapter->rx_callback_cycles, 0);
  atomic_set(&adapter->rx_callback_max_cycles, 0);
  atomic_set(&adapter->tx_callback_cycles, 0);
  atomic_set(&adapter->tx_callback_max_cycles, 0);
  atomic_set(&adapter->tx_sink_cycles, 0);
  atomic_set(&adapter->tx_sink_max_cycles, 0);
  atomic_set(&adapter->active_service_calls, 0);
  atomic_set(&adapter->active_service_cycles, 0);
  atomic_set(&adapter->active_service_max_cycles, 0);
  atomic_set(&adapter->errors, 0);
}

void wl_zephyr_usb_bulk_get_stats(const wl_zephyr_usb_bulk_t *adapter,
                                  wl_zephyr_usb_bulk_stats_t *out_stats) {
  if (adapter == NULL || out_stats == NULL) {
    return;
  }

  out_stats->rx_claims = (uint32_t)atomic_get(&adapter->rx_claims);
  out_stats->rx_completions =
      (uint32_t)atomic_get(&adapter->rx_completions);
  out_stats->rx_bytes = (uint32_t)atomic_get(&adapter->rx_bytes);
  out_stats->rx_pauses = (uint32_t)atomic_get(&adapter->rx_pauses);
  out_stats->tx_submissions = (uint32_t)atomic_get(&adapter->tx_submissions);
  out_stats->tx_completions = (uint32_t)atomic_get(&adapter->tx_completions);
  out_stats->tx_bytes = (uint32_t)atomic_get(&adapter->tx_bytes);
  out_stats->rx_callback_cycles =
      (uint32_t)atomic_get(&adapter->rx_callback_cycles);
  out_stats->rx_callback_max_cycles =
      (uint32_t)atomic_get(&adapter->rx_callback_max_cycles);
  out_stats->tx_callback_cycles =
      (uint32_t)atomic_get(&adapter->tx_callback_cycles);
  out_stats->tx_callback_max_cycles =
      (uint32_t)atomic_get(&adapter->tx_callback_max_cycles);
  out_stats->tx_sink_cycles =
      (uint32_t)atomic_get(&adapter->tx_sink_cycles);
  out_stats->tx_sink_max_cycles =
      (uint32_t)atomic_get(&adapter->tx_sink_max_cycles);
  out_stats->active_service_calls =
      (uint32_t)atomic_get(&adapter->active_service_calls);
  out_stats->active_service_cycles =
      (uint32_t)atomic_get(&adapter->active_service_cycles);
  out_stats->active_service_max_cycles =
      (uint32_t)atomic_get(&adapter->active_service_max_cycles);
  out_stats->errors = (uint32_t)atomic_get(&adapter->errors);
  out_stats->enabled = atomic_test_bit(&adapter->flags, ADAPTER_ENABLED);
  out_stats->rx_active = atomic_test_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
  out_stats->tx_active = atomic_test_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
  out_stats->rx_fault = wl_zephyr_usb_bulk_rx_fault(adapter);
  out_stats->last_rx_fault = (wl_zephyr_usb_rx_fault_t)atomic_get(&adapter->last_rx_fault);
  out_stats->recovering = atomic_test_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
}

static wl_err_t transport_configure_rx(wl_transport_t *transport, size_t minimum,
                                        wl_transport_rx_override_t *out) {
  wl_zephyr_usb_bulk_t *adapter = (wl_zephyr_usb_bulk_t *)transport;
  const wl_transport_rx_override_t *rx = &adapter->config.rx;
  if (rx->rx_storage != NULL || rx->rx_storage_size != 0U || rx->rx_unit_size != 0U)
    *out = *rx;
  const size_t mps = USBD_SUPPORTS_HIGH_SPEED ? WL_USB_HS_MPS : WL_USB_FS_MPS;
  if (out->rx_storage == NULL || out->rx_storage_size < out->rx_unit_size ||
      out->rx_unit_size < minimum || out->rx_unit_size % mps != 0U)
    return WL_ERR_INVALID_ARG;
  return WL_OK;
}

static wl_err_t transport_attach(wl_transport_t *transport, const wl_transport_attach_t *info) {
  wl_zephyr_usb_bulk_t *adapter = (wl_zephyr_usb_bulk_t *)transport;
  wl_zephyr_usb_bulk_config_t config = adapter->config;
  if (info->framing != WL_NODE_NATIVE) return WL_ERR_INVALID_ARG;
  return initialize(adapter, &config, info);
}

static wl_packet_submit_t transport_submit(wl_transport_t *transport, const uint8_t *data,
                                            size_t length, wl_packet_token_t token) {
  return wl_zephyr_usb_bulk_submit(transport, data, length, token);
}
static void transport_notify(wl_transport_t *transport) {
  wl_zephyr_usb_bulk_notify(transport);
}
static wl_err_t transport_service(wl_transport_t *transport, uint32_t now) {
  (void)now;
  wl_zephyr_usb_bulk_t *adapter = (wl_zephyr_usb_bulk_t *)transport;
  if (atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) return WL_OK;
  wl_err_t result = wl_zephyr_usb_bulk_service(adapter);
  /* At the node boundary, a full ring is normal: let the endpoint drain it.
   * Readiness requests another service pass when a slot becomes writable. */
  return result == WL_ERR_WOULD_BLOCK ? WL_OK : result;
}
static wl_io_readiness_t transport_readiness(const wl_transport_t *transport, uint32_t now) {
  (void)now;
  return wl_io_readiness_at(wl_zephyr_usb_bulk_ready((const wl_zephyr_usb_bulk_t *)transport) ? 0U : UINT32_MAX);
}
static wl_err_t transport_wait(wl_transport_t *transport, uint32_t maximum_ms) {
  return wl_zephyr_usb_bulk_wait((wl_zephyr_usb_bulk_t *)transport,
                                 wl_zephyr_timeout_ms(maximum_ms));
}
static wl_err_t transport_stop(wl_transport_t *transport) {
  return stop((wl_zephyr_usb_bulk_t *)transport, true);
}

static const wl_transport_ops_t transport_ops = {
  .configure_rx = transport_configure_rx,
  .attach = transport_attach, .submit = transport_submit, .notify = transport_notify,
  .service = transport_service, .readiness = transport_readiness,
  .wait = transport_wait, .stop = transport_stop,
};

wl_transport_t *wl_zephyr_usb_bulk_transport(wl_zephyr_usb_bulk_t *adapter,
    const wl_zephyr_usb_bulk_config_t *config) {
  if (adapter == NULL || config == NULL) return NULL;
  adapter->transport.ops = &transport_ops;
  adapter->config = *config;
  return &adapter->transport;
}
