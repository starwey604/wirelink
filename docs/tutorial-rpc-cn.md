# 同步 handler 的 RPC

start 接纳时快照请求，每个方向可并发四次，第五次返回 WOULD_BLOCK。已接纳调用只完成一次，接纳失败不会再回调。handler 立即返回有符号业务状态；算术溢出以 status 1 拒绝。应用阻塞包装推进同一个 core，不建立第二套关联表。

本页对应当前 WLC/codegen 0.13 和 profile v2。源码为 C11，两个端点在同一应用 owner 上通过真实 RAM copy 连接，不打开硬件。示例 helper 的递增 instance 仅用于进程内测试；生产使用平台随机源或持久唯一分配器。

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
cmake --build build/tutorials --parallel
ctest --test-dir build/tutorials --output-on-failure
```

运行目标 `wirelink_calculator_demo` （位于对应 examples 子目录）。

[01_rpc/client.c](../examples/01_rpc/client.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "loopback_owner.h"
unsigned calculator_async_count(void);
static calculator_kernel_t endpoints[2];
static example_pair_t pair;
EXAMPLE_COMPLETION(add, add_response)
int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const calculator_kernel_handlers_t handlers = {.on_rpc_add = calculator_add};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(calculator_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  calculator_async_example(&endpoints[0], &endpoints[1]);
  for (unsigned pass = 0; pass < 500 && calculator_async_count() != 4; ++pass) example_pair_step(&pair);
  EXAMPLE_REQUIRE(calculator_async_count() == 4);
  /* A blocking application wrapper uses the same start/owner advancement. */
  add_request_value_t request = {.has_left = true, .left = INT32_MAX, .has_right = true, .right = 1};
  wl_kernel_call_handle_t handle;
  EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[1], &request, pair.now, 500,
    add_completed, NULL, &handle) == WL_OK);
  example_pair_wait(&pair, &add_count, 1);
  EXAMPLE_REQUIRE(add_result.outcome == WL_KERNEL_REJECTED && add_result.status == 1);
  example_pair_close(&pair);
  puts("four concurrent copied requests and reverse-direction rejection passed");
  return 0;
}
```

[01_rpc/server.c](../examples/01_rpc/server.c)

```c
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
```

[内核 API](static-endpoint-kernel-api.md) · [迁移清单](migration-0.13-cn.md) · [应用长任务](tutorial-rpc-deferred-cn.md)
