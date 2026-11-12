# 保存包含字符串的 RPC 结果

[English](tutorial-rpc-values.md) · [并发调用](tutorial-rpc-async-cn.md)

完成八次查询，关闭两端后继续读取已复制的 UTF-8/NUL 名称。预期输出：

```text
bounded UTF-8/NUL response copied before callback exit and retained after close
```

在 Wirelink 根目录运行，准备匹配的 WLC 0.13.0（contract 0.13），见[工具配置](development-cn.md#构建匹配-wlc-和生成教程)。C11 端点共享一个 owner 和 RAM-copy loopback，不打开硬件。一个 tick 是一次循环，不是毫秒。fixture instance ID 仅限进程内使用，生产环境使用新的平台身份。

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

## Callback 返回前转换成 owned value

生成 completion 传入的 `const info_response_t *` 仅在 callback 中有效，不能保存指针。`EXAMPLE_COMPLETION(info, info_response)` 调用 `info_response_value_from_view`，深复制有界 string/bytes 和嵌套值到 `info_response_value_t`。helper 来自 [loopback_owner.h](../examples/common/loopback_owner.h)：

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

`name.length == 11` 包含内嵌 NUL；逻辑字符串不是以该字节结束。上界按 UTF-8 字节计数。转换成功后的 `info_value` 由应用持有，即使关闭后仍可读；无论何种终态，原 handle 都失效。见[结果合同](kernel-api-cn.md#terminal-results)，下一篇是[并发与背压](tutorial-rpc-async-cn.md)。
