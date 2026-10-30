/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_UDP_PEER_BUSINESS_H
#define WIRELINK_UDP_PEER_BUSINESS_H
#include "udp_demo_kernel.h"
#include <limits.h>
static inline int32_t peer_add(void *user, const add_request_t *request, add_response_value_t *response) {
  (void)user;
  int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true; response->sum = (int32_t)sum; return 0;
}
typedef struct {
  unsigned completions, samples;
  wl_kernel_result_t result;
  add_response_value_t response;
  uint32_t sample;
} peer_client_t;
static inline void peer_completed(void *user, wl_kernel_result_t result, const add_response_t *response) {
  peer_client_t *state = user;
  if (result.outcome == WL_KERNEL_SUCCESS &&
      (!response || add_response_value_from_view(response, &state->response) != WL_CODEC_OK))
    result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  state->result = result; ++state->completions;
}
static inline void peer_sample(void *user, const telemetry_t *sample) {
  peer_client_t *state = user; state->sample = sample->sample; ++state->samples;
}
#endif
