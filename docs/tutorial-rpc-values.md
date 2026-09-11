# Part 3: query and save device information

The calculator returned a number. Now a device returns its name and firmware
version, which a UI may retain after communication stops. Ordinary responses are
**owned values**: generated structs contain bounded character arrays. Assignment
copies the characters too, without an allocator or a destructor.
[中文](tutorial-rpc-values-cn.md)。

## 1. Run two programs

Use the build from [setup](installation.md) and [the calculator](tutorial-rpc.md).
Run each command in a different terminal:

```sh
./build/tutorials/examples/02_device_info/device_info_server
./build/tutorials/examples/02_device_info/device_info_client
```

Windows multi-config binaries live under `02_device_info/Release/` and end in
`.exe`. The first client prints:

```text
current name=demo-sensor firmware=dev query=2
saved after close name=demo-sensor firmware=dev query=1
```

It makes two queries. The first line is the second response; the second line
prints the first response after endpoint close. The next client process reports
queries 4 and 3.
Stop the server with Ctrl+C. Default server/client ports are 49201/49200.

## 2. Bound the strings

Complete [device_info.wl](../examples/02_device_info/device_info.wl):

```text
version 1;

message InfoRequest @id(30) {}

message InfoResponse @id(31) {
  required string<31> name @id(1);
  required string<15> firmware @id(2);
  required uint32 query_count @id(3);
}
```

`string<31>` means at most 31 UTF-8 **bytes**, not 31 characters.
Generated `name.length` counts bytes, `name.data[]` contains them, and
`has_name` records presence. The empty request means “query this device.”
`query_count` is demonstration business data, not an internal correlation ID.

Complete [device_info.bind.wl](../examples/02_device_info/device_info.bind.wl):

```text
profile version 1;

rpc GetInfo {
  request = InfoRequest;
  response = InfoResponse;
}
```

This is still an ordinary reliable RPC; strings require no new transport model.

## 3. Server: fill an owned response

Complete [server.c](../examples/02_device_info/server.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "device_info_endpoint.h"
#include "tutorial_host.h"

static int32_t get_info(void *context, const info_request_value_t *request,
                        info_response_value_t *response) {
  uint32_t *queries = context;
  const char name[] = "demo-sensor";
  const char firmware[] = "dev";
  (void)request;
  response->has_name = response->has_firmware = response->has_query_count = true;
  response->name.length = sizeof(name) - 1U;
  memcpy(response->name.data, name, sizeof(name) - 1U);
  response->firmware.length = sizeof(firmware) - 1U;
  memcpy(response->firmware.data, firmware, sizeof(firmware) - 1U);
  response->query_count = ++*queries;
  return 0;
}

int main(int argc, char **argv) {
  static device_info_endpoint_t server;
  device_info_endpoint_config_t config;
  uint32_t queries = 0;
  uint16_t local = 49201, peer = 49200;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(device_info_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);

  config.on_get_info = get_info;
  config.get_info_user_data = &queries;
  CHECK(device_info_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(device_info_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("device info server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(device_info_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(device_info_endpoint_close(&server) == WL_OK);
  example_udp_close(udp);
  return 0;
}
```

The framework clears the response before calling the handler. Set presence and
length, then copy the actual bytes. These fixed literals fit the schema bounds;
validate external lengths before copying, never write beyond the generated array.
Local literals are used only for this copy, not retained by the framework.
`get_info_user_data` points to application state—the query counter—not protocol storage.

## 4. Client: save the value, then close communication

Complete [client.c](../examples/02_device_info/client.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static device_info_endpoint_t client;
  info_request_value_t request;
  info_response_value_t response;
  uint16_t local = 49200, peer = 49201;
  CHECK(example_ports(argc, argv, &local, &peer));
  info_request_value_clear(&request);
  CHECK(device_info_endpoint_init(&client, wl_platform_environment()) == WL_OK);
  example_udp_t *udp = example_udp_open(device_info_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);

  wl_rpc_completion_t result = device_info_endpoint_get_info_sync(&client, &request, &response, 1500U);
  if (result.status != WL_RPC_SUCCESS) {
    fprintf(stderr, "GetInfo failed: %s\n", wl_rpc_status_str(result.status));
    CHECK(device_info_endpoint_close(&client) == WL_OK);
    example_udp_close(udp);
    return 1;
  }
  const info_response_value_t saved = response; /* Copies the strings too. */
  result = device_info_endpoint_get_info_sync(&client, &request, &response, 1500U);
  CHECK(result.status == WL_RPC_SUCCESS);
  printf("current name=%.*s firmware=%.*s query=%lu\n",
         (int)response.name.length, response.name.data,
         (int)response.firmware.length, response.firmware.data,
         (unsigned long)response.query_count);
  info_response_value_clear(&response); /* Does not change saved. */
  CHECK(device_info_endpoint_close(&client) == WL_OK);
  example_udp_close(udp);

  /* These fields remain usable after another call and endpoint close. */
  printf("saved after close name=%.*s firmware=%.*s query=%lu\n",
         (int)saved.name.length, saved.name.data,
         (int)saved.firmware.length, saved.firmware.data,
         (unsigned long)saved.query_count);
  return 0;
}
```

The important line is `saved = response`. The first output line proves that the
second call updated `response` to query 2. Clearing that response and closing both
endpoint and UDP still leaves query 1 printable from `saved`. Its lifetime is that
of the ordinary C variable; this does not make arbitrary pointers immortal.
Callback response pointers still expire when the callback returns: copy the value to retain it.

Printing respects the byte length rather than assuming `strlen` describes every
valid string. Embedded NUL is permitted; `%.*s` still stops there. A complete binary
log must write by length. Strings are UTF-8, not arbitrary bytes.

## 5. The RAM cost

Bounded arrays contribute to value and endpoint storage. Ownership is stable, but
not zero-copy or unbounded. This example does not allocate a heap.
Use [advanced RPC](rpc-runtime.md) or [Bulk](bulk-performance.md) when large data or
explicit borrowed views are appropriate. Optional allocators change where endpoint
storage comes from, not response ownership.

Continue with [asynchronous clients](tutorial-rpc-async.md),
[deferred servers](tutorial-rpc-deferred.md), or [project integration](tutorial-integration.md).
