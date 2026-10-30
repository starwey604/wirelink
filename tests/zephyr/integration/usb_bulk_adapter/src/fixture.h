/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/ztest.h>

#include "wirelink/zephyr/usb_bulk.h"

#define QUEUED_REQUESTS 5U


struct queued_request {
  const struct usbd_class_data *class_data;
  struct net_buf *buffer;
};

static struct queued_request queued[QUEUED_REQUESTS];
static size_t queued_count;
static bool complete_out_inline;

int __wrap_usbd_ep_enqueue(const struct usbd_class_data *class_data,
                           struct net_buf *buffer) {
  struct udc_buf_info *info = udc_get_buf_info(buffer);

  if (complete_out_inline && !USB_EP_DIR_IS_IN(info->ep)) {
    complete_out_inline = false;
    zassert_ok(class_data->api->request((struct usbd_class_data *)class_data,
                                       buffer, 0));
    return 0;
  }
  info->owner = (void *)class_data;
  for (size_t index = 0U; index < queued_count; ++index) {
    if (queued[index].buffer == NULL) {
      queued[index] =
          (struct queued_request){.class_data = class_data, .buffer = buffer};
      return 0;
    }
  }
  zassert_true(queued_count < ARRAY_SIZE(queued));
  queued[queued_count++] =
      (struct queued_request){.class_data = class_data, .buffer = buffer};
  return 0;
}

int __wrap_usbd_ep_buf_free(struct usbd_context *context,
                            struct net_buf *buffer) {
  ARG_UNUSED(context);
  net_buf_unref(buffer);
  return 0;
}

static struct usbd_class_data *bulk_class(void) {
  STRUCT_SECTION_FOREACH_ALTERNATE(usbd_class_fs, usbd_class_node, node) {
    if (strcmp(node->c_data->name, "wirelink_bulk_0") == 0) {
      return node->c_data;
    }
  }
  return NULL;
}

static struct usb_ep_descriptor *bulk_descriptor(
    struct usbd_class_data *class_data, enum usbd_speed speed, bool in) {
  struct usb_desc_header *const *headers =
      class_data->api->get_desc(class_data, speed);

  for (; *headers != NULL && (*headers)->bLength != 0U; ++headers) {
    if ((*headers)->bDescriptorType == USB_DESC_ENDPOINT) {
      struct usb_ep_descriptor *endpoint =
          (struct usb_ep_descriptor *)(*headers);

      if (USB_EP_DIR_IS_IN(endpoint->bEndpointAddress) == in) {
        return endpoint;
      }
    }
  }
  return NULL;
}

static struct net_buf *take_queued(uint8_t endpoint) {
  for (size_t index = 0U; index < queued_count; ++index) {
    struct net_buf *buffer = queued[index].buffer;

    if (buffer != NULL && udc_get_buf_info(buffer)->ep == endpoint) {
      queued[index].buffer = NULL;
      return buffer;
    }
  }
  return NULL;
}

static void configure_descriptor(struct usb_ep_descriptor *descriptor,
                                 uint8_t endpoint, uint16_t packet_size) {
  zassert_not_null(descriptor);
  descriptor->bEndpointAddress = endpoint;
  descriptor->wMaxPacketSize = sys_cpu_to_le16(packet_size);
}


static int disable_result, enable_result;
static unsigned disable_calls, enable_calls;
int __wrap_usbd_disable(struct usbd_context *context) {
  ARG_UNUSED(context);
  ++disable_calls;
  if (disable_result == 0) bulk_class()->api->disable(bulk_class());
  return disable_result;
}
int __wrap_usbd_enable(struct usbd_context *context) {
  ARG_UNUSED(context);
  ++enable_calls;
  /* A successful controller enable is not SET_CONFIGURATION yet. */
  return enable_result;
}
