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

NET_BUF_POOL_FIXED_DEFINE(unexpected_request_pool, 1, 0,
                          sizeof(struct udc_buf_info), NULL);

struct queued_request {
  const struct usbd_class_data *class_data;
  struct net_buf *buffer;
};

static struct queued_request queued[QUEUED_REQUESTS];
static size_t queued_count;
static uint8_t unexpected_data[1];

int __wrap_usbd_ep_enqueue(const struct usbd_class_data *class_data,
                           struct net_buf *buffer) {
  struct udc_buf_info *info = udc_get_buf_info(buffer);

  zassert_true(queued_count < ARRAY_SIZE(queued));
  info->owner = (void *)class_data;
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
  struct usb_desc_header **headers = class_data->api->get_desc(class_data, speed);

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

ZTEST(wirelink_usb_bulk_adapter, test_assigned_endpoints_mps_and_late_cancel) {
  static wl_ctx_t link;
  static wl_zephyr_usb_bulk_t adapter;
  static uint8_t rx_fifo[1024];
  static uint8_t tx_payload[128];
  static uint8_t tx_unit[256];
  static uint8_t control_unit[256];
  static uint8_t rx_fallback[256];
  static struct usbd_context context;
  static uint8_t tx_data[16];
  struct usbd_class_data *class_data = bulk_class();
  const wl_config_t link_config = {
      .max_payload_len = sizeof(tx_payload),
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_NONE,
      .session_id = 1U,
      .max_transmission_unit = sizeof(tx_unit),
  };
  const wl_storage_t storage = {
      .tx_payload = tx_payload,
      .tx_payload_size = sizeof(tx_payload),
      .tx_unit = tx_unit,
      .tx_unit_size = sizeof(tx_unit),
      .control_unit = control_unit,
      .control_unit_size = sizeof(control_unit),
      .rx_fifo = rx_fifo,
      .rx_fifo_size = sizeof(rx_fifo),
      .rx_fallback = rx_fallback,
      .rx_fallback_size = sizeof(rx_fallback),
  };
  const wl_zephyr_usb_bulk_config_t adapter_config = {
      .link = &link,
      .maximum_rx_size = 100U,
  };
  struct net_buf *fs_out_request;
  struct net_buf *fs_in_request;
  struct net_buf *hs_out_request;
  struct net_buf *hs_in_request;
  struct net_buf *reconfigured_out_request;
  struct net_buf *unexpected_request;
  struct udc_buf_info *info;
  wl_zephyr_usb_bulk_stats_t stats;
  uint32_t errors_before_unexpected;

  zassert_not_null(class_data);
  class_data->uds_ctx = &context;
  configure_descriptor(bulk_descriptor(class_data, USBD_SPEED_FS, false),
                       0x02U, 32U);
  configure_descriptor(bulk_descriptor(class_data, USBD_SPEED_FS, true),
                       0x81U, 8U);
  configure_descriptor(bulk_descriptor(class_data, USBD_SPEED_HS, false),
                       0x04U, 64U);
  configure_descriptor(bulk_descriptor(class_data, USBD_SPEED_HS, true),
                       0x83U, 64U);

  zassert_ok(wl_init(&link, &link_config, &storage));
  zassert_ok(wl_zephyr_usb_bulk_init(&adapter, &adapter_config));
  zassert_ok(class_data->api->init(class_data));

  context.status.speed = USBD_SPEED_FS;
  class_data->api->enable(class_data);
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  fs_out_request = take_queued(0x02U);
  zassert_not_null(fs_out_request);
  zassert_equal(net_buf_tailroom(fs_out_request), 96U,
                "OUT claim must use the assigned FS MPS");
  zassert_equal(adapter.rx_endpoint, 0x02U);

  zassert_ok(wl_send_unreliable(&link, 1U, tx_data, 2U));
  fs_in_request = take_queued(0x81U);
  zassert_not_null(fs_in_request);
  info = udc_get_buf_info(fs_in_request);
  zassert_true(info->zlp, "IN ZLP must use the assigned FS IN MPS");
  zassert_equal(adapter.tx_endpoint, 0x81U);

  context.status.speed = USBD_SPEED_HS;
  zassert_ok(class_data->api->request(class_data, fs_in_request,
                                     -ECONNABORTED));
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  zassert_ok(class_data->api->request(class_data, fs_out_request,
                                     -ECONNABORTED));
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));

  hs_out_request = take_queued(0x04U);
  zassert_not_null(hs_out_request);
  zassert_equal(net_buf_tailroom(hs_out_request), 64U,
                "OUT rearm must use the assigned HS MPS");
  zassert_equal(adapter.rx_endpoint, 0x04U);

  zassert_ok(wl_send_unreliable(&link, 2U, tx_data, 1U));
  hs_in_request = take_queued(0x83U);
  zassert_not_null(hs_in_request);
  info = udc_get_buf_info(hs_in_request);
  zassert_false(info->zlp, "IN ZLP must use the assigned HS IN MPS");

  class_data->api->disable(class_data);
  context.status.speed = USBD_SPEED_FS;
  zassert_ok(class_data->api->request(class_data, hs_in_request,
                                     -ECONNABORTED));
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  wl_zephyr_usb_bulk_get_stats(&adapter, &stats);
  zassert_false(stats.tx_active);
  zassert_ok(class_data->api->request(class_data, hs_out_request,
                                     -ECONNABORTED));
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));

  wl_zephyr_usb_bulk_get_stats(&adapter, &stats);
  zassert_false(stats.enabled);
  zassert_false(stats.rx_active);
  zassert_false(stats.tx_active);
  zassert_equal(adapter.rx_endpoint, 0U);
  zassert_equal(adapter.tx_endpoint, 0U);

  class_data->api->enable(class_data);
  reconfigured_out_request = take_queued(0x02U);
  zassert_not_null(reconfigured_out_request,
                   "reconfiguration must use the assigned FS OUT endpoint");
  class_data->api->disable(class_data);
  zassert_ok(class_data->api->request(class_data, reconfigured_out_request,
                                     -ECONNABORTED));
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));

  wl_zephyr_usb_bulk_get_stats(&adapter, &stats);
  errors_before_unexpected = stats.errors;
  unexpected_request =
      net_buf_alloc_with_data(&unexpected_request_pool, unexpected_data,
                              sizeof(unexpected_data), K_NO_WAIT);
  zassert_not_null(unexpected_request);
  udc_get_buf_info(unexpected_request)->ep = 0x07U;
  zassert_equal(class_data->api->request(class_data, unexpected_request, 0),
                -EINVAL);

  wl_zephyr_usb_bulk_get_stats(&adapter, &stats);
  zassert_false(stats.enabled);
  zassert_false(stats.rx_active);
  zassert_false(stats.tx_active);
  zassert_equal(adapter.rx_endpoint, 0U);
  zassert_equal(adapter.tx_endpoint, 0U);
  zassert_equal(stats.errors, errors_before_unexpected + 1U);
}

ZTEST_SUITE(wirelink_usb_bulk_adapter, NULL, NULL, NULL, NULL, NULL);
