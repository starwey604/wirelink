/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info_kernel.h"
#include <string.h>
int32_t device_info_query(void *user, const info_request_t *request, info_response_value_t *response) {
  (void)request;
  unsigned *queries = user;
  response->has_name = response->has_firmware = response->has_query_count = true;
  response->name.length = 11; memcpy(response->name.data, "workbench\0x", 11);
  response->firmware.length = 6; memcpy(response->firmware.data, "0.13.0", 6);
  response->query_count = ++*queries;
  return 0;
}
