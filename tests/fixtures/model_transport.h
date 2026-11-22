/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TEST_MODEL_TRANSPORT_H
#define WIRELINK_TEST_MODEL_TRANSPORT_H
#include <wirelink/transport.h>

/* Packet driver for tests that control each RX and completion explicitly. */
typedef struct {
  wl_transport_t base;
  wl_transport_rx_override_t rx;
  wl_packet_sink_fn sink;
  wl_packet_notify_fn wake;
  void *context;
} model_transport_t;
static inline wl_err_t model_attach(wl_transport_t *base, const wl_transport_attach_t *info) {
  (void)base;
  return info->stream == NULL ? WL_OK : WL_ERR_INVALID_STATE;
}
static inline wl_err_t model_rx(wl_transport_t *base, size_t minimum, wl_transport_rx_override_t *out) {
  (void)minimum;
  *out = ((model_transport_t *)base)->rx;
  return WL_OK;
}
static inline wl_packet_submit_t model_submit(wl_transport_t *base, const uint8_t *data,
                                              size_t length, wl_packet_token_t token) {
  model_transport_t *model = (model_transport_t *)base;
  return model->sink(model->context, data, length, token);
}
static inline void model_notify(wl_transport_t *base) {
  model_transport_t *model = (model_transport_t *)base;
  if (model->wake) model->wake(model->context);
}
static const wl_transport_ops_t model_transport_ops = {
  .configure_rx = model_rx, .attach = model_attach, .submit = model_submit, .notify = model_notify
};
#endif
