/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * Copyright (c) 2026 Wirelink contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(wirelink_usb_bulk, LOG_LEVEL_INF);

#define BULK_ENABLED 0
#define BULK_OUT_EP 0x01U
#define BULK_IN_EP 0x81U
#define BULK_FS_MPS 64U
#define BULK_HS_MPS 512U

NET_BUF_POOL_FIXED_DEFINE(bulk_pool, 1, 0, sizeof(struct udc_buf_info), NULL);
UDC_STATIC_BUF_DEFINE(bulk_buffer, BULK_HS_MPS);

struct bulk_desc {
  struct usb_if_descriptor interface;
  struct usb_ep_descriptor fs_out;
  struct usb_ep_descriptor fs_in;
  struct usb_ep_descriptor hs_out;
  struct usb_ep_descriptor hs_in;
  struct usb_desc_header terminator;
};

struct bulk_data {
  struct bulk_desc *descriptors;
  const struct usb_desc_header **fs_descriptors;
  const struct usb_desc_header **hs_descriptors;
  atomic_t state;
};

static uint8_t bulk_out_ep(struct usbd_class_data *class_data) {
  ARG_UNUSED(class_data);
  return BULK_OUT_EP;
}

static uint8_t bulk_in_ep(struct usbd_class_data *class_data) {
  ARG_UNUSED(class_data);
  return BULK_IN_EP;
}

static struct net_buf *bulk_buffer_allocate(struct usbd_class_data *class_data,
                                            uint8_t endpoint) {
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  const size_t size = usbd_bus_speed(context) == USBD_SPEED_HS
                          ? BULK_HS_MPS
                          : BULK_FS_MPS;
  struct net_buf *buffer =
      net_buf_alloc_with_data(&bulk_pool, bulk_buffer, size, K_NO_WAIT);
  struct udc_buf_info *info;

  if (buffer == NULL) {
    return NULL;
  }
  net_buf_reset(buffer);
  info = udc_get_buf_info(buffer);
  info->ep = endpoint;
  return buffer;
}

static int bulk_submit_out(struct usbd_class_data *class_data) {
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct net_buf *buffer =
      bulk_buffer_allocate(class_data, bulk_out_ep(class_data));
  int result;

  if (buffer == NULL) {
    return -ENOMEM;
  }
  result = usbd_ep_enqueue(class_data, buffer);
  if (result != 0) {
    usbd_ep_buf_free(context, buffer);
  }
  return result;
}

static int bulk_request(struct usbd_class_data *class_data,
                        struct net_buf *buffer, int error) {
  struct usbd_context *context = usbd_class_get_ctx(class_data);
  struct bulk_data *data = usbd_class_get_private(class_data);
  struct udc_buf_info *info = udc_get_buf_info(buffer);
  const uint8_t completed_endpoint = info->ep;

  if (!atomic_test_bit(&data->state, BULK_ENABLED) || error != 0) {
    usbd_ep_buf_free(context, buffer);
    if (error != -ECONNABORTED) {
      LOG_ERR("bulk transfer failed: ep=0x%02x error=%d", completed_endpoint,
              error);
    }
    return error;
  }

  if (completed_endpoint == bulk_in_ep(class_data)) {
    net_buf_reset(buffer);
    info = udc_get_buf_info(buffer);
    info->ep = bulk_out_ep(class_data);
  } else if (completed_endpoint == bulk_out_ep(class_data)) {
    /* Echo the exact OUT payload by reusing the same endpoint buffer. */
    info->ep = bulk_in_ep(class_data);
  } else {
    usbd_ep_buf_free(context, buffer);
    return -EINVAL;
  }

  error = usbd_ep_enqueue(class_data, buffer);
  if (error != 0) {
    usbd_ep_buf_free(context, buffer);
  }
  return error;
}

static void bulk_enable(struct usbd_class_data *class_data) {
  struct bulk_data *data = usbd_class_get_private(class_data);

  if (!atomic_test_and_set_bit(&data->state, BULK_ENABLED)) {
    const int result = bulk_submit_out(class_data);
    if (result != 0) {
      atomic_clear_bit(&data->state, BULK_ENABLED);
      LOG_ERR("failed to queue initial bulk OUT: %d", result);
    } else {
      LOG_INF("vendor bulk interface enabled");
    }
  }
}

static void bulk_disable(struct usbd_class_data *class_data) {
  struct bulk_data *data = usbd_class_get_private(class_data);

  atomic_clear_bit(&data->state, BULK_ENABLED);
  LOG_INF("vendor bulk interface disabled");
}

static int bulk_init(struct usbd_class_data *class_data) {
  ARG_UNUSED(class_data);
  return 0;
}

static void *bulk_get_descriptors(struct usbd_class_data *class_data,
                                  enum usbd_speed speed) {
  struct bulk_data *data = usbd_class_get_private(class_data);

  if (USBD_SUPPORTS_HIGH_SPEED && speed == USBD_SPEED_HS) {
    return data->hs_descriptors;
  }
  return data->fs_descriptors;
}

static struct usbd_class_api bulk_api = {
    .request = bulk_request,
    .get_desc = bulk_get_descriptors,
    .enable = bulk_enable,
    .disable = bulk_disable,
    .init = bulk_init,
};

static struct bulk_desc bulk_descriptors = {
    .interface =
        {
            .bLength = sizeof(struct usb_if_descriptor),
            .bDescriptorType = USB_DESC_INTERFACE,
            .bInterfaceNumber = 0U,
            .bAlternateSetting = 0U,
            .bNumEndpoints = 2U,
            .bInterfaceClass = USB_BCC_VENDOR,
            .bInterfaceSubClass = 0x57U,
            .bInterfaceProtocol = 0x4CU,
            .iInterface = 0U,
        },
    .fs_out =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = BULK_OUT_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(BULK_FS_MPS),
            .bInterval = 0U,
        },
    .fs_in =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = BULK_IN_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(BULK_FS_MPS),
            .bInterval = 0U,
        },
    .hs_out =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = BULK_OUT_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(BULK_HS_MPS),
            .bInterval = 0U,
        },
    .hs_in =
        {
            .bLength = sizeof(struct usb_ep_descriptor),
            .bDescriptorType = USB_DESC_ENDPOINT,
            .bEndpointAddress = BULK_IN_EP,
            .bmAttributes = USB_EP_TYPE_BULK,
            .wMaxPacketSize = sys_cpu_to_le16(BULK_HS_MPS),
            .bInterval = 0U,
        },
    .terminator =
        {
            .bLength = 0U,
            .bDescriptorType = 0U,
        },
};

static const struct usb_desc_header *bulk_fs_descriptors[] = {
    (struct usb_desc_header *)&bulk_descriptors.interface,
    (struct usb_desc_header *)&bulk_descriptors.fs_out,
    (struct usb_desc_header *)&bulk_descriptors.fs_in,
    (struct usb_desc_header *)&bulk_descriptors.terminator,
};

static const struct usb_desc_header *bulk_hs_descriptors[] = {
    (struct usb_desc_header *)&bulk_descriptors.interface,
    (struct usb_desc_header *)&bulk_descriptors.hs_out,
    (struct usb_desc_header *)&bulk_descriptors.hs_in,
    (struct usb_desc_header *)&bulk_descriptors.terminator,
};

static struct bulk_data bulk_private = {
    .descriptors = &bulk_descriptors,
    .fs_descriptors = bulk_fs_descriptors,
    .hs_descriptors = bulk_hs_descriptors,
};

USBD_DEFINE_CLASS(wirelink_bulk_0, &bulk_api, &bulk_private, NULL);
