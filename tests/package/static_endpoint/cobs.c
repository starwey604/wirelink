/* SPDX-License-Identifier: Apache-2.0 */
#include "probe.h"
#include <stdlib.h>
static probe_node_t endpoint;
static unsigned complete, effects;
wl_err_t probe_tx_write(probe_node_t *self, const uint8_t *data, size_t length) {
  size_t consumed = 0;
  wl_err_t error = probe_rx_feed(self, data, length, &consumed);
  if (error != WL_OK || consumed != length) abort();
  return WL_OK;
}
static int32_t execute(void *user, const request_t *request, response_t *response) {
  (void)user;
  ++effects;
  response->output = request->input;
  return 0;
}
static void done(void *user, wl_rpc_result_t result, const response_t *response) {
  (void)user;
  if (result.outcome != WL_RPC_SUCCESS || !response || response->output != INT32_MIN)
    abort();
  ++complete;
}
int main(void) {
  probe_handlers_t handlers = {.on_execute = execute};
  if (probe_init(&endpoint, probe_tx_write, &handlers, NULL)) abort();
  request_t value = {.input = INT32_MIN};
  if (probe_call_execute_ex(&endpoint, &value, 0, 100, done, NULL, NULL)) abort();
  for (unsigned i = 0; i < 10 && !complete; ++i)
    if (probe_step(&endpoint, 0)) abort();
  if (complete != 1 || effects != 1 || probe_close(&endpoint) ||
      probe_step(&endpoint, 0) || !probe_quiescent(&endpoint)) abort();
}
