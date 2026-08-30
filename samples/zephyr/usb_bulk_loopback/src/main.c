/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(wirelink_usb_bulk_main, LOG_LEVEL_INF);

static void usb_message(struct usbd_context *context,
                        const struct usbd_msg *message) {
  LOG_INF("USB message: %s", usbd_msg_type_string(message->type));

  if (usbd_can_detect_vbus(context) && message->type == USBD_MSG_VBUS_READY) {
    if (usbd_enable(context) != 0) {
      LOG_ERR("failed to enable USB device");
    }
  }
  if (usbd_can_detect_vbus(context) &&
      message->type == USBD_MSG_VBUS_REMOVED) {
    if (usbd_disable(context) != 0) {
      LOG_ERR("failed to disable USB device");
    }
  }
}

int main(void) {
  struct usbd_context *context = sample_usbd_init_device(usb_message);
  int result;

  if (context == NULL) {
    LOG_ERR("failed to initialize USB device support");
    return -ENODEV;
  }
  if (!usbd_can_detect_vbus(context)) {
    result = usbd_enable(context);
    if (result != 0) {
      LOG_ERR("failed to enable USB device: %d", result);
      return result;
    }
  }

  LOG_INF("raw vendor bulk loopback ready (OUT=0x01 IN=0x81 DMA=off)");
  return 0;
}
