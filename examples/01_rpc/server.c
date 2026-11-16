/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include <limits.h>
/* [docs:calculator_server] */
int32_t server_on_add(server_node_t *self, const add_request_t *request, add_response_t *response) {
  (void)self;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->sum = (int32_t)sum;
  return 0;
}
/* [/docs:calculator_server] */
