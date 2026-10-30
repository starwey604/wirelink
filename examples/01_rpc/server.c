/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include <limits.h>
int32_t calculator_add(void *user, const add_request_t *request, add_response_value_t *response) {
  (void)user;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true; response->sum = (int32_t)sum;
  return 0;
}
