/* SPDX-License-Identifier: Apache-2.0 */
/* K1 isolated port binding, derived from the product usb_bulk adapter.
 * Descriptor/net_buf/ZLP/reconfiguration behavior is retained. This source
 * replaces that adapter in a prototype build; it never links the old core. */

#include "wirelink/zephyr/kernel_usb_bulk.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>

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
  wl_kernel_usb_bulk_t *adapter;
};

NET_BUF_POOL_FIXED_DEFINE(wl_usb_bulk_pool, 2, 0, sizeof(struct udc_buf_info) + sizeof(wl_packet_token_t),
                          NULL);

static int queue_out(struct usbd_class_data *class_data,
                     wl_kernel_usb_bulk_t *adapter);
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

static uint8_t bulk_out_ep(struct usbd_class_data *class_data) {
  return bulk_out_descriptor(class_data)->bEndpointAddress;
}

static uint8_t bulk_in_ep(struct usbd_class_data *class_data) {
  return bulk_in_descriptor(class_data)->bEndpointAddress;
}

static size_t bulk_in_packet_size(struct usbd_class_data *class_data) {
  return sys_le16_to_cpu(bulk_in_descriptor(class_data)->wMaxPacketSize);
}

void wl_kernel_usb_bulk_notify(void *context) {
  wl_kernel_usb_bulk_t *adapter = context;
  if (adapter->wake_consumer != NULL) adapter->wake_consumer(adapter->wake_user_data);
}

static void wake_consumer(const wl_kernel_usb_bulk_t *adapter) {
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

static void latch_rx_fault(wl_kernel_usb_bulk_t *adapter,
                           wl_kernel_usb_rx_fault_t reason) {
  if (atomic_cas(&adapter->rx_fault, WL_KERNEL_USB_RX_FAULT_NONE, reason)) {
    atomic_set(&adapter->last_rx_fault, reason);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_FAULT_PENDING);
  }
  atomic_set_bit(&adapter->flags, ADAPTER_RX_FAULTED);
}

static uint32_t cycle_count(const wl_kernel_usb_bulk_t *adapter) {
  return adapter->cycle_counter == NULL
             ? 0U
             : adapter->cycle_counter(adapter->cycle_counter_user_data);
}

static void atomic_max(atomic_t *maximum, uint32_t value) {
  atomic_val_t previous = atomic_get(maximum);

  while (value > (uint32_t)previous &&
         !atomic_cas(maximum, previous, (atomic_val_t)value)) {
    previous = atomic_get(maximum);
  }
}

static void record_cycles(const wl_kernel_usb_bulk_t *adapter, atomic_t *total,
                          atomic_t *maximum, uint32_t started) {
  uint32_t elapsed;

  if (adapter->cycle_counter == NULL) {
    return;
  }
  elapsed = cycle_count(adapter) - started;
  atomic_add(total, (atomic_val_t)elapsed);
  atomic_max(maximum, elapsed);
}

wl_packet_submit_t wl_kernel_usb_bulk_submit(void *user_data, const uint8_t *data,
                                             size_t length, wl_packet_token_t token) {
  wl_kernel_usb_bulk_t *adapter = user_data;
  struct usbd_class_data *class_data;
  struct usbd_context *context;
  struct net_buf *buffer;
  struct udc_buf_info *info;
  size_t packet_size;
  uint32_t started;
  int result;

  if (adapter == NULL || data == NULL || length == 0U ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED) ||
      atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) {
    return WL_PACKET_FAILED;
  }
  if (!atomic_test_bit(&adapter->flags, ADAPTER_ENABLED)) return WL_PACKET_BUSY;
  if (atomic_test_and_set_bit(&adapter->flags, ADAPTER_TX_ACTIVE)) {
    return WL_PACKET_BUSY;
  }
  started = cycle_count(adapter);

  class_data = wl_usb_bulk_class_data();
  context = usbd_class_get_ctx(class_data);
  buffer = net_buf_alloc_with_data(&wl_usb_bulk_pool, (uint8_t *)data, length,
                                   K_NO_WAIT);
  if (buffer == NULL) {
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    record_cycles(adapter, &adapter->tx_sink_cycles,
                  &adapter->tx_sink_max_cycles, started);
    return WL_PACKET_BUSY;
  }

  info = udc_get_buf_info(buffer);
  adapter->tx_endpoint = bulk_in_ep(class_data);
  info->ep = adapter->tx_endpoint;
  packet_size = bulk_in_packet_size(class_data);
  if ((length % packet_size) == 0U) {
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
    record_cycles(adapter, &adapter->tx_sink_cycles,
                  &adapter->tx_sink_max_cycles, started);
    return result == -ENOMEM || result == -EBUSY ? WL_PACKET_BUSY
                                                  : WL_PACKET_FAILED;
  }
  atomic_inc(&adapter->tx_submissions);
  atomic_add(&adapter->tx_bytes, (atomic_val_t)length);
  record_cycles(adapter, &adapter->tx_sink_cycles,
                &adapter->tx_sink_max_cycles, started);
  return WL_PACKET_STARTED;
}

static int bulk_request_impl(struct usbd_class_data *class_data,
                        struct net_buf *buffer, int error) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct udc_buf_info *info = udc_get_buf_info(buffer);
  wl_kernel_usb_bulk_t *adapter = private_data->adapter;
  const uint8_t endpoint = info->ep;
  uint32_t started;

  if (adapter == NULL) {
    usbd_ep_buf_free(context, buffer);
    return -ENODEV;
  }
  started = cycle_count(adapter);

  if (buffer->user_data_size < sizeof(struct udc_buf_info) + sizeof(wl_packet_token_t)) {
    atomic_inc(&adapter->errors);
    usbd_ep_buf_free(context, buffer);
    return -EINVAL;
  }
  wl_packet_token_t token = buffer_token(buffer);
  if (atomic_test_bit(&adapter->flags, ADAPTER_TX_ACTIVE) &&
      endpoint == adapter->tx_endpoint && token_equal(token, adapter->tx_token)) {
    usbd_ep_buf_free(context, buffer);
    adapter->tx_token = (wl_packet_token_t){0};
    adapter->tx_endpoint = 0U;
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    atomic_inc(&adapter->tx_completions);
    if (error != 0) atomic_inc(&adapter->errors);
    int result = wl_packet_tx_publish(adapter->port, token,
        error == 0 ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR);
    if (result != WL_OK) atomic_inc(&adapter->errors);
    record_cycles(adapter, &adapter->tx_callback_cycles,
                  &adapter->tx_callback_max_cycles, started);
    return result;
  }

  if (!atomic_test_bit(&adapter->flags, ADAPTER_RX_ACTIVE) ||
      endpoint != adapter->rx_endpoint ||
      !token_equal(token, adapter->rx_unit_claim.token)) {
    atomic_inc(&adapter->errors);
    usbd_ep_buf_free(context, buffer);
    return -EINVAL;
  }

  const bool cancel_expected = atomic_test_and_clear_bit(
      &adapter->flags, ADAPTER_RX_CANCEL_EXPECTED);
  /* A full request can complete before a terminating short packet/ZLP.
   * After an IO failure its suffix is equally unsafe to treat as a frame.
   * Latch RX stopped until USBD is disabled/reset and enabled again. */
  if (atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) {
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
  } else if (error != 0 || buffer->len >= adapter->rx_unit_claim.span.length) {
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
    if (error != -ECONNABORTED ||
        (!cancel_expected && atomic_test_bit(&adapter->flags, ADAPTER_ENABLED))) {
      atomic_inc(&adapter->errors);
      latch_rx_fault(adapter, error != 0 ? WL_KERNEL_USB_RX_FAULT_TRANSFER
                                         : WL_KERNEL_USB_RX_FAULT_BOUNDARY_LOST);
    }
  } else if (buffer->len == 0U) {
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
  } else if (wl_packet_rx_commit(adapter->port, &adapter->rx_unit_claim,
                               buffer->len) != WL_OK) {
    (void)wl_packet_rx_abort(adapter->port, &adapter->rx_unit_claim);
    atomic_inc(&adapter->errors);
    latch_rx_fault(adapter, WL_KERNEL_USB_RX_FAULT_QUEUE);
  } else {
    atomic_add(&adapter->rx_bytes, (atomic_val_t)buffer->len);
  }

  memset(&adapter->rx_unit_claim, 0, sizeof(adapter->rx_unit_claim));
  adapter->rx_endpoint = 0U;
  atomic_clear_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
  usbd_ep_buf_free(context, buffer);
  if (atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULTED) ||
      atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) {
    atomic_clear_bit(&adapter->flags, ADAPTER_RX_REARM);
  } else {
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
  }
  atomic_inc(&adapter->rx_completions);
  wake_consumer(adapter);
  record_cycles(adapter, &adapter->rx_callback_cycles,
                &adapter->rx_callback_max_cycles, started);
  return 0;
}

static int bulk_request(struct usbd_class_data *class_data,
                         struct net_buf *buffer, int error) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);
  wl_kernel_usb_bulk_t *adapter = private_data->adapter;
  if (adapter == NULL) return bulk_request_impl(class_data, buffer, error);
  atomic_inc(&adapter->callbacks_active);
  int result = bulk_request_impl(class_data, buffer, error);
  /* stopped must not acknowledge while the callback still records stats or
   * accesses the old port. Copy the final outer wake before the last access. */
  wl_kernel_usb_bulk_wake_fn wake = adapter->wake_consumer;
  void *user = adapter->wake_user_data;
  atomic_dec(&adapter->callbacks_active);
  if (wake != NULL) wake(user);
  return result;
}

static void bulk_enable(struct usbd_class_data *class_data) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);
  wl_kernel_usb_bulk_t *adapter = private_data->adapter;

  if (adapter != NULL) {
    /* Repeated enable callbacks cannot recover a still-configured endpoint. */
    if (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) ||
        atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) return;
    atomic_set(&adapter->rx_fault, WL_KERNEL_USB_RX_FAULT_NONE);
    atomic_clear_bit(&adapter->flags, ADAPTER_RX_FAULTED);
    atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
    atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_DISABLED);
    atomic_set_bit(&adapter->flags, ADAPTER_ENABLED);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
    /* Only owner service claims/enqueues RX. The USB thread publishes IO. */
    wake_consumer(adapter);
  }
}

static void bulk_disable(struct usbd_class_data *class_data) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);

  if (private_data->adapter != NULL) {
    if (atomic_test_bit(&private_data->adapter->flags, ADAPTER_RX_ACTIVE)) {
      atomic_set_bit(&private_data->adapter->flags, ADAPTER_RX_CANCEL_EXPECTED);
    }
    atomic_clear_bit(&private_data->adapter->flags, ADAPTER_ENABLED);
    wake_consumer(private_data->adapter);
  }
}

static int bulk_init(struct usbd_class_data *class_data) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);

  return private_data->adapter == NULL ? -ENODEV : 0;
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
            .bInterfaceSubClass = WL_KERNEL_USB_BULK_INTERFACE_SUBCLASS,
            .bInterfaceProtocol = WL_KERNEL_USB_BULK_INTERFACE_PROTOCOL,
            .iInterface = 0U,
        },
    .fs_out =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_KERNEL_USB_BULK_OUT_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(WL_USB_FS_MPS),
            .bInterval = 0U,
        },
    .fs_in =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_KERNEL_USB_BULK_IN_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(WL_USB_FS_MPS),
            .bInterval = 0U,
        },
    .hs_out =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_KERNEL_USB_BULK_OUT_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(WL_USB_HS_MPS),
            .bInterval = 0U,
        },
    .hs_in =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = WL_KERNEL_USB_BULK_IN_EP,
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

static int queue_out(struct usbd_class_data *class_data,
                     wl_kernel_usb_bulk_t *adapter) {
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct net_buf *buffer;
  struct udc_buf_info *info;
  wl_packet_claim_t unit_claim;
  size_t request_size;
  int result;

  if (!atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) ||
      atomic_test_bit(&adapter->flags, ADAPTER_RX_ACTIVE) ||
      atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULTED) ||
      atomic_test_bit(&adapter->flags, ADAPTER_STOPPING)) {
    return WL_OK;
  }
  memset(&unit_claim, 0, sizeof(unit_claim));
  result = wl_packet_rx_claim(adapter->port, adapter->maximum_rx_size, &unit_claim);
  if (result != WL_OK) {
    atomic_inc(&adapter->rx_pauses);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
    return result;
  }
  request_size = unit_claim.span.length;
  buffer = net_buf_alloc_with_data(&wl_usb_bulk_pool,
                                   unit_claim.span.data,
                                   request_size, K_NO_WAIT);
  if (buffer == NULL) {
    (void)wl_packet_rx_abort(adapter->port, &unit_claim);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
    return WL_ERR_WOULD_BLOCK;
  }
  net_buf_reset(buffer);
  info = udc_get_buf_info(buffer);
  adapter->rx_endpoint = bulk_out_ep(class_data);
  info->ep = adapter->rx_endpoint;

  adapter->rx_unit_claim = unit_claim;
  set_buffer_token(buffer, unit_claim.token);
  /* A completion may run before enqueue returns. Consume the old request
   * before submission so its completion's new rearm request is preserved. */
  atomic_clear_bit(&adapter->flags, ADAPTER_RX_REARM);
  atomic_set_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
  result = usbd_ep_enqueue(class_data, buffer);
  if (result != 0) {
    atomic_clear_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
    adapter->rx_endpoint = 0U;
    (void)wl_packet_rx_abort(adapter->port, &unit_claim);
    usbd_ep_buf_free(context, buffer);
    atomic_inc(&adapter->errors);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
    return WL_ERR_IO;
  }

  atomic_inc(&adapter->rx_claims);
  return WL_OK;
}

int wl_kernel_usb_bulk_init(wl_kernel_usb_bulk_t *adapter,
                            const wl_kernel_usb_bulk_config_t *config) {

  if (adapter == NULL || config == NULL || config->port == NULL ||
      private_data.adapter != NULL || config->maximum_frame_size < 4U ||
      config->maximum_frame_size >= config->port->config.rx_unit_size ||
      config->port->config.submit != wl_kernel_usb_bulk_submit ||
      config->port->config.user != adapter ||
      !atomic_load_explicit(&config->port->running, memory_order_acquire)) {
    return WL_ERR_INVALID_ARG;
  }
  const wl_kernel_usb_bulk_config_t value = *config;
  memset(adapter, 0, sizeof(*adapter));
  adapter->port = value.port;
  adapter->maximum_rx_size = value.port->config.rx_unit_size;
  adapter->wake_user_data = value.wake_user_data;
  adapter->wake_consumer = value.wake_consumer;
  adapter->cycle_counter_user_data = value.cycle_counter_user_data;
  adapter->cycle_counter = value.cycle_counter;
  private_data.adapter = adapter;
  atomic_set_bit(&adapter->flags, ADAPTER_INITIALIZED);
  return WL_OK;
}

int wl_kernel_usb_bulk_service(wl_kernel_usb_bulk_t *adapter) {
  uint32_t started;
  bool active;
  int result;

  if (adapter == NULL ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) {
    return WL_ERR_INVALID_ARG;
  }

  started = cycle_count(adapter);
  active = atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) &&
           atomic_test_bit(&adapter->flags, ADAPTER_RX_REARM) &&
           !atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULTED) &&
           !atomic_test_bit(&adapter->flags, ADAPTER_STOPPING);
  if (active) {
    result = queue_out(&wirelink_bulk_0, adapter);
  } else {
    result = WL_OK;
  }
  if (active) {
    atomic_inc(&adapter->active_service_calls);
    record_cycles(adapter, &adapter->active_service_cycles,
                  &adapter->active_service_max_cycles, started);
  }
  const bool report_fault = atomic_test_and_clear_bit(&adapter->flags,
                                                      ADAPTER_RX_FAULT_PENDING);
  return report_fault || atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULTED)
             ? WL_ERR_IO : result;
}

int wl_kernel_usb_bulk_ready(const wl_kernel_usb_bulk_t *adapter) {
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) return 0;
  return atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULT_PENDING) ||
      (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) &&
       atomic_test_bit(&adapter->flags, ADAPTER_RX_REARM) &&
       !atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULTED) &&
       !atomic_test_bit(&adapter->flags, ADAPTER_STOPPING) &&
       wl_packet_rx_available(adapter->port));
}

int wl_kernel_usb_bulk_stop(wl_kernel_usb_bulk_t *adapter) {
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED))
    return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_INVALID_STATE;
  atomic_set_bit(&adapter->flags, ADAPTER_STOPPING);
  wl_packet_stop(adapter->port);
  struct usbd_context *context = usbd_class_get_ctx(wl_usb_bulk_class_data());
  if (context == NULL) return WL_ERR_NOT_INITIALIZED;
  if (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) && usbd_disable(context) != 0)
    return WL_ERR_IO;
  return wl_kernel_usb_bulk_stopped(adapter) ? WL_OK : WL_ERR_WOULD_BLOCK;
}

int wl_kernel_usb_bulk_stopped(const wl_kernel_usb_bulk_t *adapter) {
  return adapter != NULL && atomic_test_bit(&adapter->flags, ADAPTER_STOPPING) &&
      atomic_get(&adapter->callbacks_active) == 0 &&
      !atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) &&
      !atomic_test_bit(&adapter->flags, ADAPTER_RX_ACTIVE) &&
      !atomic_test_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
}

int wl_kernel_usb_bulk_deinit(wl_kernel_usb_bulk_t *adapter) {
  if (adapter == NULL || private_data.adapter != adapter ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) return WL_ERR_INVALID_ARG;
  if (!wl_kernel_usb_bulk_stopped(adapter) || !wl_packet_quiescent(adapter->port))
    return WL_ERR_WOULD_BLOCK;
  private_data.adapter = NULL;
  atomic_clear_bit(&adapter->flags, ADAPTER_INITIALIZED);
  return WL_OK;
}

wl_kernel_usb_rx_fault_t wl_kernel_usb_bulk_rx_fault(
    const wl_kernel_usb_bulk_t *adapter) {
  return adapter == NULL ? WL_KERNEL_USB_RX_FAULT_NONE
                         : (wl_kernel_usb_rx_fault_t)atomic_get(&adapter->rx_fault);
}

int wl_kernel_usb_bulk_recover(wl_kernel_usb_bulk_t *adapter) {
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

void wl_kernel_usb_bulk_reset_stats(wl_kernel_usb_bulk_t *adapter) {
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

void wl_kernel_usb_bulk_get_stats(const wl_kernel_usb_bulk_t *adapter,
                                  wl_kernel_usb_bulk_stats_t *out_stats) {
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
  out_stats->rx_fault = wl_kernel_usb_bulk_rx_fault(adapter);
  out_stats->last_rx_fault = (wl_kernel_usb_rx_fault_t)atomic_get(&adapter->last_rx_fault);
  out_stats->recovering = atomic_test_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
}
