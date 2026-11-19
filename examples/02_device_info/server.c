/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include <string.h>
/* [docs:device_info_server] */
int32_t info_server_on_get_info(info_server_node_t *self, const info_request_t *request, info_response_t *response) {
  (void)request;
  unsigned *queries = self->user;
  response->name.length = 11; memcpy(response->name.data, "workbench\0x", 11);
  response->firmware.length = 6; memcpy(response->firmware.data, "0.0.99", 6);
  response->query_count = ++*queries;
  return 0;
}
/* [/docs:device_info_server] */
