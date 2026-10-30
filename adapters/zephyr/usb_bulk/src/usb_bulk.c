/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/zephyr/usb_bulk.h"
#include "wirelink/frame.h"

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
};

enum tx_completion {
  TX_COMPLETION_NONE,
  TX_COMPLETION_DONE,
  TX_COMPLETION_FAILED,
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
};

NET_BUF_POOL_FIXED_DEFINE(wl_usb_bulk_pool, 2, 0, sizeof(struct udc_buf_info),
                          NULL);

static int queue_out(struct usbd_class_data *class_data,
                     wl_zephyr_usb_bulk_t *adapter);
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

static void wake_consumer(const wl_zephyr_usb_bulk_t *adapter) {
  if (adapter->wake_consumer != NULL) {
    adapter->wake_consumer(adapter->wake_user_data);
  }
}

static void latch_rx_fault(wl_zephyr_usb_bulk_t *adapter,
                           wl_usb_bulk_rx_fault_t reason) {
  if (atomic_cas(&adapter->rx_fault, WL_USB_BULK_RX_FAULT_NONE, reason)) {
    atomic_set(&adapter->last_rx_fault, reason);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_FAULT_PENDING);
  }
  atomic_set_bit(&adapter->flags, ADAPTER_RX_FAULTED);
}

static uint32_t cycle_count(const wl_zephyr_usb_bulk_t *adapter) {
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

static void record_cycles(const wl_zephyr_usb_bulk_t *adapter, atomic_t *total,
                          atomic_t *maximum, uint32_t started) {
  uint32_t elapsed;

  if (adapter->cycle_counter == NULL) {
    return;
  }
  elapsed = cycle_count(adapter) - started;
  atomic_add(total, (atomic_val_t)elapsed);
  atomic_max(maximum, elapsed);
}

static wl_sink_result_t usb_sink(void *user_data, wl_io_token_t token,
                                 const uint8_t *data, size_t length) {
  wl_zephyr_usb_bulk_t *adapter = user_data;
  struct usbd_class_data *class_data;
  struct usbd_context *context;
  struct net_buf *buffer;
  struct udc_buf_info *info;
  size_t packet_size;
  uint32_t started;
  int result;

  if (adapter == NULL || data == NULL || length == 0U ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED) ||
      !atomic_test_bit(&adapter->flags, ADAPTER_ENABLED)) {
    return WL_SINK_FAILED;
  }
  if (atomic_test_and_set_bit(&adapter->flags, ADAPTER_TX_ACTIVE)) {
    return WL_SINK_BUSY;
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
    return WL_SINK_BUSY;
  }

  info = udc_get_buf_info(buffer);
  adapter->tx_endpoint = bulk_in_ep(class_data);
  info->ep = adapter->tx_endpoint;
  packet_size = bulk_in_packet_size(class_data);
  if ((length % packet_size) == 0U) {
    udc_ep_buf_set_zlp(buffer);
  }

  adapter->tx_token = token;
  atomic_set(&adapter->tx_completion, TX_COMPLETION_NONE);
  result = usbd_ep_enqueue(class_data, buffer);
  if (result != 0) {
    adapter->tx_token = 0U;
    adapter->tx_endpoint = 0U;
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    usbd_ep_buf_free(context, buffer);
    record_cycles(adapter, &adapter->tx_sink_cycles,
                  &adapter->tx_sink_max_cycles, started);
    return result == -ENOMEM || result == -EBUSY ? WL_SINK_BUSY
                                                  : WL_SINK_FAILED;
  }
  atomic_inc(&adapter->tx_submissions);
  atomic_add(&adapter->tx_bytes, (atomic_val_t)length);
  record_cycles(adapter, &adapter->tx_sink_cycles,
                &adapter->tx_sink_max_cycles, started);
  return WL_SINK_STARTED;
}

static int bulk_request(struct usbd_class_data *class_data,
                        struct net_buf *buffer, int error) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct udc_buf_info *info = udc_get_buf_info(buffer);
  wl_zephyr_usb_bulk_t *adapter = private_data->adapter;
  const uint8_t endpoint = info->ep;
  uint32_t started;

  if (adapter == NULL) {
    usbd_ep_buf_free(context, buffer);
    return -ENODEV;
  }
  started = cycle_count(adapter);

  if (atomic_test_bit(&adapter->flags, ADAPTER_TX_ACTIVE) &&
      endpoint == adapter->tx_endpoint) {
    usbd_ep_buf_free(context, buffer);
    atomic_set(&adapter->tx_completion,
               error == 0 ? TX_COMPLETION_DONE : TX_COMPLETION_FAILED);
    wake_consumer(adapter);
    record_cycles(adapter, &adapter->tx_callback_cycles,
                  &adapter->tx_callback_max_cycles, started);
    return 0;
  }

  if (!atomic_test_bit(&adapter->flags, ADAPTER_RX_ACTIVE) ||
      endpoint != adapter->rx_endpoint) {
    atomic_inc(&adapter->errors);
    usbd_ep_buf_free(context, buffer);
    return -EINVAL;
  }

  const bool cancel_expected = atomic_test_and_clear_bit(
      &adapter->flags, ADAPTER_RX_CANCEL_EXPECTED);
  /* A full request can complete before a terminating short packet/ZLP.
   * After an IO failure its suffix is equally unsafe to treat as a frame.
   * Latch RX stopped until USBD is disabled/reset and enabled again. */
  if (error != 0 || buffer->len >= adapter->rx_unit_claim.span.length) {
    (void)wl_rx_unit_abort(adapter->link, &adapter->rx_unit_claim);
    if (error != -ECONNABORTED ||
        (!cancel_expected && atomic_test_bit(&adapter->flags, ADAPTER_ENABLED))) {
      atomic_inc(&adapter->errors);
      latch_rx_fault(adapter, error != 0 ? WL_USB_BULK_RX_FAULT_TRANSFER
                                         : WL_USB_BULK_RX_FAULT_BOUNDARY_LOST);
    }
  } else if (buffer->len == 0U) {
    (void)wl_rx_unit_abort(adapter->link, &adapter->rx_unit_claim);
  } else if (wl_rx_unit_commit(adapter->link, &adapter->rx_unit_claim,
                               buffer->len) != WL_OK) {
    (void)wl_rx_unit_abort(adapter->link, &adapter->rx_unit_claim);
    atomic_inc(&adapter->errors);
    latch_rx_fault(adapter, WL_USB_BULK_RX_FAULT_QUEUE);
  } else {
    atomic_add(&adapter->rx_bytes, (atomic_val_t)buffer->len);
  }

  memset(&adapter->rx_unit_claim, 0, sizeof(adapter->rx_unit_claim));
  adapter->rx_endpoint = 0U;
  atomic_clear_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
  usbd_ep_buf_free(context, buffer);
  if (atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULTED)) {
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

static void bulk_enable(struct usbd_class_data *class_data) {
  struct bulk_class_data *private_data = usbd_class_get_private(class_data);
  wl_zephyr_usb_bulk_t *adapter = private_data->adapter;

  if (adapter != NULL) {
    /* Repeated enable callbacks cannot recover a still-configured endpoint. */
    if (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED)) return;
    atomic_set(&adapter->rx_fault, WL_USB_BULK_RX_FAULT_NONE);
    atomic_clear_bit(&adapter->flags, ADAPTER_RX_FAULTED);
    atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
    atomic_clear_bit(&adapter->flags, ADAPTER_RECOVERY_DISABLED);
    atomic_set_bit(&adapter->flags, ADAPTER_ENABLED);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
    (void)queue_out(class_data, adapter);
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

static int queue_out(struct usbd_class_data *class_data,
                     wl_zephyr_usb_bulk_t *adapter) {
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct net_buf *buffer;
  struct udc_buf_info *info;
  wl_rx_unit_claim_t unit_claim;
  size_t request_size;
  int result;

  if (!atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) ||
      atomic_test_bit(&adapter->flags, ADAPTER_RX_ACTIVE)) {
    return WL_OK;
  }
  memset(&unit_claim, 0, sizeof(unit_claim));
  result = wl_rx_unit_claim(adapter->link, adapter->maximum_rx_size, &unit_claim);
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
    (void)wl_rx_unit_abort(adapter->link, &unit_claim);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
    return WL_ERR_WOULD_BLOCK;
  }
  net_buf_reset(buffer);
  info = udc_get_buf_info(buffer);
  adapter->rx_endpoint = bulk_out_ep(class_data);
  info->ep = adapter->rx_endpoint;

  adapter->rx_unit_claim = unit_claim;
  /* A completion may run before enqueue returns. Consume the old request
   * before submission so its completion's new rearm request is preserved. */
  atomic_clear_bit(&adapter->flags, ADAPTER_RX_REARM);
  atomic_set_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
  result = usbd_ep_enqueue(class_data, buffer);
  if (result != 0) {
    atomic_clear_bit(&adapter->flags, ADAPTER_RX_ACTIVE);
    adapter->rx_endpoint = 0U;
    (void)wl_rx_unit_abort(adapter->link, &unit_claim);
    usbd_ep_buf_free(context, buffer);
    atomic_inc(&adapter->errors);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_REARM);
    return WL_ERR_IO;
  }

  atomic_inc(&adapter->rx_claims);
  return WL_OK;
}

int wl_zephyr_usb_bulk_init(wl_zephyr_usb_bulk_t *adapter,
                            const wl_zephyr_usb_bulk_config_t *config) {
  int result;

  if (adapter == NULL || config == NULL || config->link == NULL ||
      private_data.adapter != NULL) {
    return WL_ERR_INVALID_ARG;
  }
  result = wl_usb_bulk_config_check_rx(config->link, config->transport);
  if (result != WL_OK) return result;

  memset(adapter, 0, sizeof(*adapter));
  adapter->link = config->link;
  adapter->maximum_rx_size = config->transport.rx_capacity;
  adapter->wake_user_data = config->wake_user_data;
  adapter->wake_consumer = config->wake_consumer;
  adapter->cycle_counter_user_data = config->cycle_counter_user_data;
  adapter->cycle_counter = config->cycle_counter;
  private_data.adapter = adapter;
  result = wl_set_sink(adapter->link, usb_sink, adapter);
  if (result != WL_OK) {
    private_data.adapter = NULL;
    return result;
  }
  atomic_set_bit(&adapter->flags, ADAPTER_INITIALIZED);
  return WL_OK;
}

int wl_zephyr_usb_bulk_service(wl_zephyr_usb_bulk_t *adapter) {
  atomic_val_t completion;
  uint32_t started;
  bool active;
  int result;

  if (adapter == NULL ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) {
    return WL_ERR_INVALID_ARG;
  }

  started = cycle_count(adapter);
  completion = atomic_set(&adapter->tx_completion, TX_COMPLETION_NONE);
  active = completion != TX_COMPLETION_NONE ||
           (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) &&
            atomic_test_bit(&adapter->flags, ADAPTER_RX_REARM));
  if (completion != TX_COMPLETION_NONE) {
    wl_io_token_t token = adapter->tx_token;

    adapter->tx_token = 0U;
    adapter->tx_endpoint = 0U;
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    atomic_inc(&adapter->tx_completions);
    result = wl_tx_consumed(adapter->link, token,
                            completion == TX_COMPLETION_DONE ? WL_OK
                                                             : WL_ERR_IO);
    if (result != WL_OK) {
      atomic_inc(&adapter->errors);
      if (active) {
        atomic_inc(&adapter->active_service_calls);
        record_cycles(adapter, &adapter->active_service_cycles,
                      &adapter->active_service_max_cycles, started);
      }
      return result;
    }
  }

  if (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) &&
      atomic_test_bit(&adapter->flags, ADAPTER_RX_REARM)) {
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

wl_readiness_t wl_zephyr_usb_bulk_readiness(const wl_zephyr_usb_bulk_t *adapter) {
  wl_readiness_t value = wl_readiness_idle();
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) return value;
  value.runnable = atomic_test_bit(&adapter->flags, ADAPTER_RX_FAULT_PENDING) ||
                   atomic_get(&adapter->tx_completion) != TX_COMPLETION_NONE;
  value.blocked = atomic_test_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
  if (atomic_test_bit(&adapter->flags, ADAPTER_ENABLED) &&
      atomic_test_bit(&adapter->flags, ADAPTER_RX_REARM)) {
    const bool room = wl_rx_can_accept_unit(adapter->link);
    value.runnable |= room;
    value.blocked |= !room;
  }
  return value;
}

wl_usb_bulk_rx_fault_t wl_zephyr_usb_bulk_rx_fault(
    const wl_zephyr_usb_bulk_t *adapter) {
  return adapter == NULL ? WL_USB_BULK_RX_FAULT_NONE
                         : (wl_usb_bulk_rx_fault_t)atomic_get(&adapter->rx_fault);
}

int wl_zephyr_usb_bulk_recover(wl_zephyr_usb_bulk_t *adapter) {
  int result;
  if (adapter == NULL ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_INVALID_STATE;
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
  out_stats->last_rx_fault = (wl_usb_bulk_rx_fault_t)atomic_get(&adapter->last_rx_fault);
  out_stats->recovering = atomic_test_bit(&adapter->flags, ADAPTER_RECOVERY_REQUESTED);
}
