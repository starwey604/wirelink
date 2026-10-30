/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CPP_TEST_ENDPOINT_H_
#define WIRELINK_CPP_TEST_ENDPOINT_H_
#include "wirelink/host/endpoint.h"
#include "probe_values.h"
#ifdef __cplusplus
extern "C" {
#endif
size_t cpp_test_size(void);
size_t cpp_test_alignment(void);
wl_err_t cpp_test_prepare(void *, wl_host_endpoint_t *, int echo);
wl_err_t cpp_test_initialize(void *, uint64_t, wl_packet_sink_fn, wl_packet_notify_fn, void *);
typedef struct {
  void *endpoint;
  const request_value_t *request;
  response_value_t *response;
  wl_host_rpc_notify_fn notify;
  void *user;
  unsigned callbacks;
} cpp_test_call_t;
wl_err_t cpp_test_submit(cpp_test_call_t *, uint32_t, uint32_t, wl_host_rpc_notify_fn,
                         void *, wl_kernel_call_handle_t *);
#ifdef __cplusplus
}
#endif
#endif
