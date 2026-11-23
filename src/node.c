/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/node.h"

static wl_packet_submit_t submit(void *user, const uint8_t *data, size_t length,
                                 wl_packet_token_t token) {
  wl_node_t *node = user;
  if (node->transport && node->transport->ops->submit)
    return node->transport->ops->submit(node->transport, data, length, token);
  if (!node->sync_tx) return WL_PACKET_FAILED;
  wl_err_t error = node->sync_tx(node, data, length);
  if (error == WL_OK) return WL_PACKET_SENT;
  return error == WL_ERR_BUSY || error == WL_ERR_WOULD_BLOCK ? WL_PACKET_BUSY : WL_PACKET_FAILED;
}

static void notify(void *user) {
  wl_node_t *node = user;
  node->transport->ops->notify(node->transport);
}

static void report_error(void *user, uint16_t id, wl_err_t error) {
  wl_node_t *node = user;
  if (node->on_error) node->on_error(node->user, id, error);
}

static wl_err_t bind_node(wl_node_t *node, const wl_node_layout_t *layout,
                          wl_transport_t *transport, wl_node_sync_tx_fn sync_tx) {
  if (!node || !layout || (layout->stream != NULL) != (layout->stream_ops != NULL))
    return WL_ERR_INVALID_ARG;
  const wl_transport_ops_t *ops = transport ? transport->ops : NULL;
  if ((transport && (!ops || !ops->attach)) ||
      (layout->has_tx && !sync_tx && !(ops && ops->submit)))
    return WL_ERR_INVALID_ARG;
  const wl_node_stream_ops_t *stream = layout->stream_ops;
  if (stream && (!stream->init || !stream->submit || !stream->feed || !stream->abort_rx ||
                 !stream->notify)) return WL_ERR_INVALID_ARG;
  size_t minimum = (size_t)layout->rx_wire_max + 1U;
  wl_transport_rx_override_t rx = {0};
  if (ops && ops->configure_rx) {
    wl_err_t error = ops->configure_rx(transport, minimum, &rx);
    if (error != WL_OK) return error;
  }
  if (!rx.rx_storage && !rx.rx_unit_size && !rx.rx_storage_size)
    rx = (wl_transport_rx_override_t){layout->rx_storage, layout->rx_storage_size, minimum};
  if (!rx.rx_storage || rx.rx_unit_size < minimum ||
      rx.rx_unit_size > rx.rx_storage_size / WL_PACKET_RX_SLOTS) return WL_ERR_INVALID_ARG;
  uint32_t instance = layout->instance;
  if (!instance) {
    wl_err_t error = wl_packet_instance_next(&instance);
    if (error != WL_OK) return error;
  }
  node->transport = transport;
  node->transport_stopped = transport == NULL;
  node->stream = layout->stream;
  node->stream_ops = stream;
  node->sync_tx = sync_tx;
  node->user = layout->user;
  node->on_error = layout->on_error;
  node->rx_wire_max = layout->rx_wire_max;
  node->tx_wire_max = layout->tx_wire_max;
  wl_packet_notify_fn wake = ops && ops->notify ? notify : NULL;
  wl_packet_config_t packet = {
    .instance = instance, .rx_storage = rx.rx_storage, .rx_unit_size = rx.rx_unit_size,
    .rx_storage_size = rx.rx_storage_size, .submit = stream ? stream->submit : submit,
    .notify = wake && stream ? stream->notify : wake,
    .user = stream ? (void *)node->stream : (void *)node
  };
  wl_err_t error = wl_packet_init(&node->port, &packet);
  if (error != WL_OK) {
    node->transport = NULL;
    node->core.port = NULL;
    return error;
  }
  if (stream) {
    wl_packet_stream_config_t config = {
      .port = &node->port, .tx_storage = layout->stream_tx_wire,
      .physical_submit = submit, .notify = wake, .user = node,
      .maximum_frame_size = layout->tx_frame_max, .maximum_rx_frame_size = layout->rx_frame_max
    };
    error = stream->init(node->stream, &config);
  }
  if (error == WL_OK) {
    wl_endpoint_config_t config = {
      .port = &node->port, .storage = layout->storage, .route = layout->route,
      .on_error = report_error, .user = node
    };
    error = wl_endpoint_init(&node->core, &config);
  }
  if (error == WL_OK && ops) {
    const wl_transport_attach_t info = {
      &node->port, node->stream, layout->rx_wire_max, layout->tx_wire_max,
      stream ? stream->framing : WL_NODE_NATIVE
    };
    error = ops->attach(transport, &info);
    if (error != WL_OK) {
      /* attach has already torn down the driver, including its wake target. */
      node->port.config.notify = NULL;
      if (stream) (void)stream->abort_rx(node->stream);
    }
  }
  if (error != WL_OK) {
    /* No producer can remain registered after a failed attachment. */
    node->port.config.notify = NULL;
    wl_packet_stop(&node->port);
    node->transport = NULL;
    node->core.port = NULL;
  }
  return error;
}

wl_err_t wl_node_bind_transport(wl_node_t *node, const wl_node_layout_t *layout,
                                wl_transport_t *transport) {
  return transport ? bind_node(node, layout, transport, NULL) : WL_ERR_INVALID_ARG;
}

wl_err_t wl_node_bind_sync(wl_node_t *node, const wl_node_layout_t *layout,
                           wl_node_sync_tx_fn tx_write) {
  return bind_node(node, layout, NULL, tx_write);
}

wl_err_t wl_node_step(wl_node_t *node, uint32_t now_ms) {
  if (!node || !node->core.port) return WL_ERR_INVALID_ARG;
  if (node->core.in_step) return WL_ERR_REENTRANT;
  wl_err_t error = WL_OK;
  if (!node->transport_stopped && node->transport->ops->service)
    error = node->transport->ops->service(node->transport, now_ms);
  wl_err_t stepped = wl_endpoint_step(&node->core, now_ms, WL_ENDPOINT_BUDGET);
  return error == WL_OK || error == WL_ERR_WOULD_BLOCK ? stepped : error;
}

wl_io_readiness_t wl_node_readiness(const wl_node_t *node, uint32_t now_ms) {
  wl_io_readiness_t value = wl_io_readiness_idle();
  if (!node || !node->core.port) return value;
  uint32_t deadline = 0;
  int timed = 0;
  int runnable = wl_endpoint_ready(&node->core, now_ms, &timed, &deadline);
  if (timed) value = wl_io_readiness_at(deadline - now_ms);
  value.runnable |= runnable != 0;
  if (!node->transport_stopped && node->transport->ops->readiness)
    wl_io_readiness_merge(&value, node->transport->ops->readiness(node->transport, now_ms));
  return value;
}

wl_err_t wl_node_wait(wl_node_t *node, uint32_t now_ms, uint32_t max_wait_ms) {
  if (!node || !node->core.port) return WL_ERR_INVALID_ARG;
  if (node->core.in_step) return WL_ERR_REENTRANT;
  wl_io_readiness_t ready = wl_node_readiness(node, now_ms);
  if (ready.runnable || node->transport_stopped || !node->transport->ops->wait) return WL_OK;
  if (ready.next_deadline_ms < max_wait_ms) max_wait_ms = ready.next_deadline_ms;
  wl_err_t error = node->transport->ops->wait(node->transport, max_wait_ms);
  return error == WL_ERR_NO_DATA ? WL_OK : error;
}

wl_err_t wl_node_poll(wl_node_t *node, wl_clock_t clock, uint32_t max_wait_ms) {
  if (!node || !node->core.port || !clock.now_ms) return WL_ERR_INVALID_ARG;
  if (node->core.in_step) return WL_ERR_REENTRANT;
  if (max_wait_ms != 0U) {
    wl_err_t error = wl_node_wait(node, clock.now_ms(clock.user_data), max_wait_ms);
    if (error != WL_OK) return error;
  }
  return wl_node_step(node, clock.now_ms(clock.user_data));
}

wl_err_t wl_node_close(wl_node_t *node, uint32_t now_ms) {
  if (!node || !node->core.port) return WL_ERR_INVALID_ARG;
  if (node->core.in_step) return WL_ERR_REENTRANT;
  if (!node->core.closed) {
    wl_err_t error = wl_endpoint_close(&node->core);
    if (error != WL_OK) return error;
  }
  if (!node->transport_stopped) {
    wl_transport_t *transport = node->transport;
    wl_err_t error = transport->ops->stop ? transport->ops->stop(transport) : WL_OK;
    if (error == WL_ERR_WOULD_BLOCK) {
      wl_err_t stepped = wl_node_step(node, now_ms);
      return stepped == WL_OK ? error : stepped;
    }
    if (error != WL_OK) return error;
    node->transport_stopped = 1U;
  }
  if (node->stream_ops) {
    wl_err_t error = node->stream_ops->abort_rx(node->stream);
    if (error != WL_OK) return error;
  }
  wl_err_t error = wl_endpoint_step(&node->core, now_ms, WL_ENDPOINT_BUDGET);
  if (error != WL_OK) return error;
  return wl_node_quiescent(node) ? WL_OK : WL_ERR_BUSY;
}

int wl_node_quiescent(const wl_node_t *node) {
  return node && node->core.port && node->transport_stopped && wl_endpoint_quiescent(&node->core);
}

void wl_node_tx_ready(wl_node_t *node) {
  if (node && node->core.port) wl_packet_activity(&node->port);
}

wl_err_t wl_node_cancel(wl_node_t *node, wl_call_handle_t handle) {
  return node && node->core.port ? wl_endpoint_cancel(&node->core, handle) : WL_ERR_INVALID_ARG;
}

wl_err_t wl_node_rx_packet(wl_node_t *node, const uint8_t *data, size_t length) {
  if (!node || !node->core.port) return WL_ERR_INVALID_ARG;
  if (node->stream_ops) return WL_ERR_INVALID_STATE;
  if (length > node->rx_wire_max) return WL_ERR_BUF_TOO_SMALL;
  return wl_packet_rx_push(&node->port, data, length);
}

wl_err_t wl_node_rx_feed(wl_node_t *node, const uint8_t *data, size_t length, size_t *consumed) {
  if (!consumed) return WL_ERR_INVALID_ARG;
  *consumed = 0;
  if (!node || !node->core.port) return WL_ERR_INVALID_ARG;
  if (!node->stream_ops) return WL_ERR_INVALID_STATE;
  return node->stream_ops->feed(node->stream, data, length, consumed);
}
