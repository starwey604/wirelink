# Retain an RPC result containing strings

[中文](tutorial-rpc-values-cn.md) · [Concurrent calls](tutorial-rpc-async.md)

Complete eight queries, close both endpoints, then read the copied UTF-8/NUL name. Expect:

```text
bounded UTF-8/NUL response copied before callback exit and retained after close
```

Run from the Wirelink root with matching WLC 0.13.0 (contract 0.13); see [tool setup](development.md#build-matching-wlc-and-generated-tutorials). The C11 endpoints share one owner and a RAM-copy loopback, without hardware. Its tick is one loop pass, not a millisecond. Fixture instance IDs are process-local; production uses a fresh platform identity.

```sh
cmake -S . -B /tmp/wirelink-docs-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-tutorials --parallel \
  --target wirelink_device_info_demo
ctest --test-dir /tmp/wirelink-docs-tutorials --output-on-failure \
  -R '^wirelink_device_info_demo$'
/tmp/wirelink-docs-tutorials/examples/02_device_info/wirelink_device_info_demo
```

## Convert to an owned value before returning

The generated completion passes `const info_response_t *` only for the callback lifetime; do not save its pointer. `EXAMPLE_COMPLETION(info, info_response)` calls `info_response_value_from_view` to copy bounded strings/bytes and nested values into `info_response_value_t`. The helper comes from [loopback_owner.h](../examples/common/loopback_owner.h):

```c
#define EXAMPLE_COMPLETION(name, type) \
  static unsigned name##_count; \
  static wl_kernel_result_t name##_result; \
  static type##_value_t name##_value; \
  static void name##_completed(void *user, wl_kernel_result_t result, const type##_t *view) { \
    (void)user; name##_result = result; \
    if (result.outcome == WL_KERNEL_SUCCESS) \
      EXAMPLE_REQUIRE(view && type##_value_from_view(view, &name##_value) == WL_CODEC_OK); \
    ++name##_count; \
  }
```

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

`name.length == 11` includes embedded NUL; that byte does not terminate the logical string. Bounds count UTF-8 bytes. After conversion, application-owned `info_value` remains readable after close; the original handle expires at any terminal. See the [result contract](kernel-api.md#terminal-results), then [concurrency and pressure](tutorial-rpc-async.md).
