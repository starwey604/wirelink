# 管理四路调用、背压与取消

[English](tutorial-rpc-async.md) · [应用 job](tutorial-jobs-cn.md)

本例接纳四次调用，验证第五次背压，取消其中一笔，让另外三笔到期，再恢复真实 RAM-copy 传输并成功调用。它验证每次已接纳调用只交付一个终态，接纳失败没有 completion。

在 Wirelink 根目录运行，准备匹配的 WLC 0.13.0（contract 0.13），见[工具配置](development-cn.md#构建匹配-wlc-和生成教程)。C11 端点共享一个 owner 和 RAM-copy loopback，不打开硬件。一个 tick 是一次循环，不是毫秒。fixture instance ID 仅限进程内使用，生产环境使用新的平台身份。

```sh
cmake -S . -B /tmp/wirelink-docs-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-tutorials --parallel \
  --target wirelink_rpc_lifecycle
ctest --test-dir /tmp/wirelink-docs-tutorials --output-on-failure \
  -R '^wirelink_rpc_lifecycle$'
/tmp/wirelink-docs-tutorials/examples/01_rpc/wirelink_rpc_lifecycle
```

```text
four calls, fifth blocked, cancel, three timeouts and recovery passed
```

[01_rpc/lifecycle.c](../examples/01_rpc/lifecycle.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "loopback_owner.h"

static calculator_kernel_t endpoints[2];
static example_pair_t pair;
static unsigned completed, effects;
static struct completion {
  unsigned count;
  wl_kernel_result_t result;
} completions[5];

static int32_t add(void *user, const add_request_t *request, add_response_value_t *response) {
  ++effects;
  return calculator_add(user, request, response);
}

static void done(void *user, wl_kernel_result_t result, const add_response_t *response) {
  struct completion *completion = user;
  EXAMPLE_REQUIRE(completion->count == 0);
  completion->result = result;
  ++completion->count;
  ++completed;
  if (result.outcome == WL_KERNEL_SUCCESS)
    EXAMPLE_REQUIRE(response && response->sum == 42);
  else
    EXAMPLE_REQUIRE(response == NULL);
}

int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const calculator_kernel_handlers_t handlers = {.on_rpc_add = add};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(calculator_kernel_init(&endpoints[side], &pair.ports[side],
      &handlers, NULL, NULL) == WL_OK);
  wl_kernel_call_handle_t handles[4], rejected;
  add_request_value_t request = {.has_left = true, .left = 20, .has_right = true, .right = 22};
  for (unsigned i = 0; i < 4; ++i)
    EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[0], &request, 0, 2,
      done, &completions[i], &handles[i]) == WL_OK);
  EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[0], &request, 0, 2,
    done, &completions[4], &rejected) == WL_ERR_WOULD_BLOCK);
  EXAMPLE_REQUIRE(completed == 0);

  /* Hold the transport: one request is leased, the other three stay queued. */
  EXAMPLE_REQUIRE(calculator_kernel_step(&endpoints[0], 0) == WL_OK);
  EXAMPLE_REQUIRE(wl_kernel_cancel(&endpoints[0].core, handles[0]) == WL_OK);
  EXAMPLE_REQUIRE(completed == 0); /* cancel schedules, not invokes, completion. */
  EXAMPLE_REQUIRE(calculator_kernel_step(&endpoints[0], 1) == WL_OK);
  EXAMPLE_REQUIRE(completions[0].result.outcome == WL_KERNEL_CANCELLED);
  EXAMPLE_REQUIRE(wl_kernel_cancel(&endpoints[0].core, handles[0]) == WL_ERR_NOT_FOUND);
  EXAMPLE_REQUIRE(calculator_kernel_step(&endpoints[0], 2) == WL_OK);
  EXAMPLE_REQUIRE(completed == 4 && completions[4].count == 0);
  for (unsigned i = 1; i < 4; ++i)
    EXAMPLE_REQUIRE(completions[i].result.outcome == WL_KERNEL_TIMED_OUT);
  EXAMPLE_REQUIRE(wl_packet_tx_active(&pair.ports[0]) && effects == 0);

  /* Resume real copying. Local cancellation did not retract the leased request. */
  pair.now = 2;
  EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[0], &request, pair.now, 500,
    done, &completions[4], &rejected) == WL_OK);
  example_pair_wait(&pair, &completed, 5);
  EXAMPLE_REQUIRE(completions[4].result.outcome == WL_KERNEL_SUCCESS && effects == 2);
  example_pair_close(&pair);
  for (unsigned i = 0; i < 5; ++i) EXAMPLE_REQUIRE(completions[i].count == 1);
  puts("four calls, fifth blocked, cancel, three timeouts and recovery passed");
  return 0;
}
```

## 重试的是未接纳工作

第五次返回 `WL_ERR_WOULD_BLOCK`，尚未创建调用；在资源可用后可重新提交。四个 call 槽与四份 request snapshot 的生命周期独立，物理租约未归还时，即使逻辑槽空闲也可能背压。保存 `WL_OK` 返回的 handle，仅在 waiting 时 cancel；重复 cancel 或终态后的 handle 返回 `WL_ERR_NOT_FOUND`。

`cancel` 只安排后续 step 的终态。这里第一笔已租用请求取消后仍保留物理 TX，另三笔在 tick `2` 到期且未发送。恢复 loopback 后，取消请求和新请求各执行一次，所以 `effects == 2`；迟到响应不重复回调。应用计数只验证交付，不参与 RPC 匹配或计时。

Callback 可提交/取消调用，不能递归 step/close。保留 view 应像[保存结果教程](tutorial-rpc-values-cn.md)一样复制；长任务用[应用 job](tutorial-jobs-cn.md)。
