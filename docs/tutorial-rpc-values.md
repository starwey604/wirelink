# Retaining bounded results

The completion helper copies the borrowed response using the public value conversion before returning. This example validates eight replies and reads the owned UTF-8/NUL result after both endpoints close. Bounded strings carry a length; embedded NUL does not terminate the logical value. No terminal inspect/release slot remains.

This page uses current WLC/codegen 0.13 and profile v2. The C11 peers share one application owner and a real RAM-copy loopback, without opening hardware. Incrementing fixture instance IDs are only for this isolated example; production needs platform randomness or a durable unique allocator.

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
cmake --build build/tutorials --parallel
ctest --test-dir build/tutorials --output-on-failure
```

Run target `wirelink_device_info_demo` (under its examples subdirectory).

[02_device_info/client.c](../examples/02_device_info/client.c)

```c
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
```

[02_device_info/server.c](../examples/02_device_info/server.c)

```c
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
```

[Kernel API](static-endpoint-kernel-api.md) · [Migration](migration-0.13.md) · [Application jobs](tutorial-rpc-deferred.md)
