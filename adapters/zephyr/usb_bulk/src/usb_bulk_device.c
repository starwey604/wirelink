/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/usb_bulk.h"

#if defined(CONFIG_WIRELINK_ZEPHYR_USB_BULK_DEVICE)
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>

USBD_DEVICE_DEFINE(wl_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   CONFIG_WIRELINK_ZEPHYR_USB_BULK_VID, CONFIG_WIRELINK_ZEPHYR_USB_BULK_PID);
USBD_DESC_LANG_DEFINE(language);
USBD_DESC_MANUFACTURER_DEFINE(manufacturer, CONFIG_WIRELINK_ZEPHYR_USB_BULK_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(product, CONFIG_WIRELINK_ZEPHYR_USB_BULK_PRODUCT);
#if defined(CONFIG_HWINFO)
USBD_DESC_SERIAL_NUMBER_DEFINE(serial);
#endif
USBD_CONFIGURATION_DEFINE(full_speed, 0, CONFIG_WIRELINK_ZEPHYR_USB_BULK_MAX_POWER, NULL);
USBD_CONFIGURATION_DEFINE(high_speed, 0, CONFIG_WIRELINK_ZEPHYR_USB_BULK_MAX_POWER, NULL);

static K_MUTEX_DEFINE(device_lock);
static bool configured, initialized, requested, vbus_present;

static wl_err_t connection_result(int err) {
  return err == 0 || err == -EALREADY ? WL_OK : WL_ERR_IO;
}

static void usb_message(struct usbd_context *ctx, const struct usbd_msg *msg) {
  if (msg->type != USBD_MSG_VBUS_READY && msg->type != USBD_MSG_VBUS_REMOVED) return;
  if (!usbd_can_detect_vbus(ctx)) return;
  k_mutex_lock(&device_lock, K_FOREVER);
  vbus_present = msg->type == USBD_MSG_VBUS_READY;
  if (requested) {
    if (vbus_present) (void)usbd_enable(ctx);
    else (void)usbd_disable(ctx);
  }
  k_mutex_unlock(&device_lock);
}

static int add_configuration(enum usbd_speed speed, struct usbd_config_node *config) {
  int err = usbd_add_configuration(&wl_usbd, speed, config);
  if (err != 0) return err;
  err = usbd_register_class(&wl_usbd, "wirelink_bulk_0", speed, 1);
  if (err != 0) return err;
  return usbd_device_set_code_triple(&wl_usbd, speed, 0, 0, 0);
}

static int setup(void) {
  struct usbd_desc_node *const descriptors[] = {
    &language, &manufacturer, &product,
#if defined(CONFIG_HWINFO)
    &serial,
#endif
  };
  for (size_t i = 0; i < ARRAY_SIZE(descriptors); ++i) {
    int err = usbd_add_descriptor(&wl_usbd, descriptors[i]);
    if (err != 0 && err != -EALREADY) return err;
  }
  if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&wl_usbd) == USBD_SPEED_HS) {
    int err = add_configuration(USBD_SPEED_HS, &high_speed);
    if (err != 0) return err;
  }
  int err = add_configuration(USBD_SPEED_FS, &full_speed);
  if (err == 0) err = usbd_msg_register_cb(&wl_usbd, usb_message);
  return err;
}

wl_err_t wl_zephyr_usb_bulk_device_enable(void) {
  k_mutex_lock(&device_lock, K_FOREVER);
  if (!initialized) {
    /* The controller can fail initialization after the descriptors and class
     * are registered. Retry the controller, not the class registration. */
    if (!configured) configured = setup() == 0;
    if (!configured || usbd_init(&wl_usbd) != 0) {
      k_mutex_unlock(&device_lock);
      return WL_ERR_IO;
    }
    initialized = true;
  }
  requested = true;
  wl_err_t result = WL_OK;
  if (!usbd_can_detect_vbus(&wl_usbd) || vbus_present)
    result = connection_result(usbd_enable(&wl_usbd));
  k_mutex_unlock(&device_lock);
  return result;
}

wl_err_t wl_zephyr_usb_bulk_device_disable(void) {
  k_mutex_lock(&device_lock, K_FOREVER);
  requested = false;
  wl_err_t result = initialized ? connection_result(usbd_disable(&wl_usbd)) : WL_OK;
  k_mutex_unlock(&device_lock);
  return result;
}
#endif
