/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info_kernel.h"
#include "loopback_owner.h"
int32_t device_info_query(void *, const info_request_t *, info_response_value_t *);
static device_info_kernel_t endpoints[2];
static example_pair_t pair;
EXAMPLE_COMPLETION(info, info_response)
int main(void) {
  unsigned queries = 0;
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const device_info_kernel_handlers_t handlers = {.on_rpc_get_info = device_info_query};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(device_info_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, &queries) == WL_OK);
  for (unsigned call = 0; call < 8; ++call) {
    info_request_value_t request = {0}; wl_kernel_call_handle_t handle;
    EXAMPLE_REQUIRE(device_info_kernel_get_info_start(&endpoints[0], &request, pair.now, 500,
      info_completed, NULL, &handle) == WL_OK);
    example_pair_wait(&pair, &info_count, call + 1);
    EXAMPLE_REQUIRE(info_result.outcome == WL_KERNEL_SUCCESS && info_value.query_count == call + 1);
  }
  example_pair_close(&pair);
  EXAMPLE_REQUIRE(info_value.name.length == 11 && memcmp(info_value.name.data, "workbench\0x", 11) == 0);
  puts("bounded UTF-8/NUL response copied before callback exit and retained after close");
  return 0;
}
