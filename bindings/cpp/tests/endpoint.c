/* SPDX-License-Identifier: Apache-2.0 */
#include "endpoint.h"
#include "probe_kernel.h"
#include <string.h>
typedef struct {
  probe_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][2053];
  int echo;
} owner_t;
size_t cpp_test_size(void) { return sizeof(owner_t); }
size_t cpp_test_alignment(void) { return _Alignof(owner_t); }
wl_err_t cpp_test_prepare(void *storage, wl_host_endpoint_t *driver, int echo) {
  owner_t *owner = storage;
  memset(owner, 0, sizeof(*owner)); owner->echo = echo;
  *driver = (wl_host_endpoint_t){owner, &owner->endpoint.core, &owner->port, 2052};
  return WL_OK;
}
static int32_t execute(void *user, const request_t *request, response_value_t *response) {
  (void)user;
  if (request->input == INT32_MIN) return WL_ERR_TIMEOUT; /* Remote business code. */
  response->has_output = true; response->output = request->input; return 0;
}
wl_err_t cpp_test_initialize(void *storage, uint64_t instance, wl_packet_sink_fn sink,
                            wl_packet_notify_fn notify, void *user) {
  owner_t *owner = storage;
  wl_packet_config_t port = {.instance = instance, .rx_storage = &owner->rx[0][0],
    .rx_storage_size = sizeof(owner->rx), .rx_unit_size = sizeof(owner->rx[0]),
    .submit = sink, .notify = notify, .user = user};
  wl_err_t status = wl_packet_init(&owner->port, &port);
  if (status != WL_OK) return status;
  probe_kernel_handlers_t handlers = {.on_rpc_execute = owner->echo ? execute : NULL};
  return probe_kernel_init(&owner->endpoint, &owner->port, &handlers, NULL, owner);
}
static void done(void *user, wl_kernel_result_t result, const response_t *response) {
  cpp_test_call_t *call = user;
  ++call->callbacks;
  if (result.outcome == WL_KERNEL_SUCCESS &&
      (response == NULL || response_value_from_view(response, call->response) != WL_CODEC_OK))
    result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  call->notify(call->user, result);
}
wl_err_t cpp_test_submit(cpp_test_call_t *call, uint32_t now, uint32_t timeout,
                         wl_host_rpc_notify_fn notify, void *user, wl_kernel_call_handle_t *handle) {
  owner_t *owner = call->endpoint;
  call->notify = notify; call->user = user;
  return probe_kernel_execute_start(&owner->endpoint, call->request, now, timeout, done, call, handle);
}
