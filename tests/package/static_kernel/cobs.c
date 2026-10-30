/* SPDX-License-Identifier: Apache-2.0 */
#include "probe_kernel.h"
#include "probe_kernel_transport.h"
#include <stdlib.h>
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static probe_kernel_t endpoint;
static unsigned complete, effects;
static wl_packet_submit_t physical(void *user, const uint8_t *data, size_t length,
                                   wl_packet_token_t token) {
  (void)user;
  (void)token;
  size_t used = 0;
  if (probe_kernel_transport_feed(&stream, data, length, &used) != WL_OK || used != length)
    abort();
  return WL_PACKET_SENT;
}
static int32_t execute(void *user, const request_t *request, response_value_t *response) {
  (void)user;
  ++effects;
  response->has_output = true;
  response->output = request->input;
  return 0;
}
static void done(void *user, wl_kernel_result_t result, const response_t *response) {
  (void)user;
  if (result.outcome != WL_KERNEL_SUCCESS || !response || response->output != INT32_MIN)
    abort();
  ++complete;
}
int main(void) {
  static uint8_t rx[2][256], wire[256];
  if (probe_kernel_transport_init(&stream, &port, 3366, (wl_span_t){&rx[0][0], sizeof(rx)}, 256,
                                  (wl_span_t){wire, sizeof(wire)}, physical, NULL, NULL))
    abort();
  probe_kernel_handlers_t handlers = {.on_rpc_execute = execute};
  if (probe_kernel_init(&endpoint, &port, &handlers, NULL, NULL))
    abort();
  request_value_t value = {.has_input = true, .input = INT32_MIN};
  wl_kernel_call_handle_t call;
  if (probe_kernel_execute_start(&endpoint, &value, 0, 100, done, NULL, &call))
    abort();
  for (unsigned i = 0; i < 10 && !complete; ++i)
    if (probe_kernel_step(&endpoint, 0))
      abort();
  if (complete != 1 || effects != 1 || wl_kernel_close(&endpoint.core) ||
      probe_kernel_step(&endpoint, 0) || !wl_kernel_quiescent(&endpoint.core))
    abort();
}
