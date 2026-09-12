/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include <stdint.h>

int32_t calculator_test_add(void* context, const add_request_value_t* request,
                            add_response_value_t* response) {
  (void)context;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}
