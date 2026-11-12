# 发起 RPC、处理拒绝和 timeout

[English](tutorial-rpc.md) · [并发调用](tutorial-rpc-async-cn.md)

运行 calculator 示例：四个请求得到 `42`，反向调用的有符号加法溢出被业务 status `1` 拒绝。随后用 lifecycle 示例观察本地 timeout 和取消：

```text
four concurrent copied requests and reverse-direction rejection passed
four calls, fifth blocked, cancel, three timeouts and recovery passed
```

在 Wirelink 根目录运行，准备匹配的 WLC 0.13.0（contract 0.13），见[工具配置](development-cn.md#构建匹配-wlc-和生成教程)。C11 端点共享一个 owner 和 RAM-copy loopback，不打开硬件。一个 tick 是一次循环，不是毫秒。fixture instance ID 仅限进程内使用，生产环境使用新的平台身份。

```sh
cmake -S . -B /tmp/wirelink-docs-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-tutorials --parallel \
  --target wirelink_calculator_demo wirelink_rpc_lifecycle
ctest --test-dir /tmp/wirelink-docs-tutorials --output-on-failure \
  -R '^wirelink_(calculator_demo|rpc_lifecycle)$'
/tmp/wirelink-docs-tutorials/examples/01_rpc/wirelink_calculator_demo
```

```sh
/tmp/wirelink-docs-tutorials/examples/01_rpc/wirelink_rpc_lifecycle
```

## 请求与结果

`calculator_kernel_add_start` 接收 `add_request_value_t`、owner 的 `now`、timeout、callback/context 和输出 handle。只有 `WL_OK` 创建调用；输入在返回前已快照，任何失败都不会产生未来 completion。下文 `500` 是 loop tick。同步应用等待通过推进 owner 完成，不在 handler 内阻塞。

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

## 同步 handler 返回业务状态

handler 在本次 owner callback 中填好响应。`0` 表示成功，非零有符号值表示业务拒绝，拒绝响应没有业务 body。生成 route 在解码业务请求、执行 handler 前 reserve reply 存储；保留响应的 TX 返回 BUSY 不重跑 handler。

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

## Deadline 与远端副作用

[lifecycle 源码](../examples/01_rpc/lifecycle.c)在 tick `0` 接纳四次调用，timeout 为 `2`，暂不推进传输；tick `2` 的 step 将仍排队的三个调用交付为 `WL_KERNEL_TIMED_OUT / WL_ERR_TIMEOUT`。排队时间计入 timeout，到期相等即触发且先于排队响应。

取消/超时只结束本地调用，不撤销已租用请求或远端执行；恢复传输后，先前取消的请求仍会执行，但不会交付第二次终态。重试会产生新 operation，需要避免重复副作用时使用应用幂等键。完整终态见 [API 参考](kernel-api-cn.md#terminal-results)。继续阅读[保存结果](tutorial-rpc-values-cn.md)。
