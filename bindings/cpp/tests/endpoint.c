/* SPDX-License-Identifier: Apache-2.0 */
#include "endpoint.h"
#include "probe.h"
#include <string.h>
typedef struct {
  probe_node_t endpoint;
  int echo;
} owner_t;
size_t cpp_test_size(void) { return sizeof(owner_t); }
size_t cpp_test_alignment(void) { return _Alignof(owner_t); }
wl_err_t cpp_test_prepare(void *storage, wl_host_endpoint_t *driver, int echo) {
  owner_t *owner = storage;
  memset(owner, 0, sizeof(*owner)); owner->echo = echo;
  *driver = (wl_host_endpoint_t){owner, &owner->endpoint.core, &owner->endpoint.port, PROBE_RX_WIRE_MAX, PROBE_TX_WIRE_MAX};
  return WL_OK;
}
static int32_t execute(void *user, const request_t *request, response_t *response) {
  (void)user;
  if (request->input == INT32_MIN) return WL_ERR_TIMEOUT; /* Remote business code. */

  response->output = request->input;
  return 0;
}
wl_err_t cpp_test_initialize(void *storage, uint32_t instance, wl_packet_sink_fn sink,
                            wl_packet_notify_fn notify, void *user) {
  owner_t *owner = storage;
  const wl_packet_io_t io = {.submit = sink, .notify = notify, .user = user};
  probe_handlers_t handlers = {.on_execute = owner->echo ? execute : NULL};
  return probe_init_io(&owner->endpoint, instance, &io, &handlers, owner);
}
static void done(void *user, wl_rpc_result_t result, const response_t *response) {
  cpp_test_call_t *call = user;
  ++call->callbacks;
  if (result.outcome == WL_RPC_SUCCESS && response == NULL) {
    result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  } else if (result.outcome == WL_RPC_SUCCESS) {
    *call->response = *response;
  }
  call->notify(call->user, result);
}
wl_err_t cpp_test_submit(cpp_test_call_t *call, uint32_t now, uint32_t timeout,
                         wl_host_rpc_notify_fn notify, void *user, wl_call_handle_t *handle) {
  owner_t *owner = call->endpoint;
  call->notify = notify; call->user = user;
  return probe_call_execute_ex(&owner->endpoint, call->request, now, timeout, done, call, handle);
}
