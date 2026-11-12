# 接纳并推进应用 job

[English](tutorial-jobs.md) · [并发调用](tutorial-rpc-async-cn.md)

运行设备服务示例：一次 StartSelfTest 返回非零 job ID，紧接的第二次 Start 被 `DEVICE_BUSY` 拒绝；应用循环推进任务，随后普通消息携带同一 job ID 和计算结果。预期输出：

```text
device/calculator handlers, application job ID, ordinary completion and retained results passed
```

在 Wirelink 根目录运行，准备匹配的 WLC 0.13.0（contract 0.13），见[工具配置](development-cn.md#构建匹配-wlc-和生成教程)。C11 端点共享一个 owner 和 RAM-copy loopback，不打开硬件。一个 tick 是一次循环，不是毫秒。fixture instance ID 仅限进程内使用，生产环境使用新的平台身份。

```sh
cmake -S . -B /tmp/wirelink-docs-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-tutorials --parallel \
  --target wirelink_device_service_demo
ctest --test-dir /tmp/wirelink-docs-tutorials --output-on-failure \
  -R '^wirelink_device_service_demo$'
/tmp/wirelink-docs-tutorials/examples/03_device_service/wirelink_device_service_demo
```

## 短 RPC 返回 job ID

[`device.wl`](../examples/03_device_service/device.wl)定义请求 StartSelfTest `132`、响应 `133` 和普通 SelfTestProgress `134`。handler 在应用 `device_service_t` 中接纳一个 job，返回 ID 后即完成本次 RPC。`self_test_poll` 在 owner 上检查 due、计算结果并尝试普通发送；snapshot 忙时保存 job 状态，下次重试。

[03_device_service/self_test.c](../examples/03_device_service/self_test.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "self_test.h"
#include "loopback_owner.h"
static int32_t immediate(void *user, const self_test_request_t *request,
                         self_test_response_value_t *response) {
  (void)user;
  response->has_observed = true;
  response->observed = request->pattern ^ UINT32_C(0xa5a5);
  return 0;
}
static int32_t start(void *user, const start_self_test_request_t *request,
                     start_self_test_response_value_t *response) {
  device_service_t *state = user;
  if (state->job_pending || state->next_job == UINT32_MAX) return DEVICE_BUSY;
  state->job_id = ++state->next_job;
  state->pattern = request->pattern;
  state->due = state->now + 20U;
  state->job_pending = true;
  response->has_job_id = true;
  response->job_id = state->job_id;
  return 0;
}
void self_test_bind(device_kernel_handlers_t *handlers) {
  handlers->on_rpc_self_test = immediate;
  handlers->on_rpc_start_self_test = start;
}
/* Application event-loop work, with ordinary TX backpressure. No reply token. */
void self_test_poll(device_service_t *state, device_kernel_t *endpoint) {
  if (!state->job_pending || (int32_t)(state->now - state->due) < 0) return;
  self_test_progress_value_t progress = {.has_job_id = true, .job_id = state->job_id,
    .has_observed = true, .observed = state->pattern ^ UINT32_C(0xa5a5)};
  const wl_err_t error = device_kernel_send_self_test_progress(endpoint, &progress);
  if (error == WL_ERR_WOULD_BLOCK) return;
  EXAMPLE_REQUIRE(error == WL_OK);
  state->job_pending = false;
}
```

## 读取任务状态与完成结果

[client.c](../examples/03_device_service/client.c)的 `advance` 设置 `service.now`，调用 `self_test_poll`，再推进两个端点。`due = now + 20U` 是应用的 20 个循环 tick，不是另一次 RPC timeout。owner 可直接查询 `job_id`、`job_pending` 和 `due`；`job_pending` 包括等待普通消息接纳的阶段，消息接纳成功才清零，不表示对端已收到。

客户端通过 `progress` callback 复制 `job_id/observed`，再与 Start 响应的 ID 比较。示例仅保留一份 job 状态，没有远端查询 RPC 或完成历史。需要远端查询时，应在应用协议中定义 `job_id` 查询请求、running/done/unknown 状态、结果保留期限及重启身份；这些信息不能从本地 RPC timeout 推断。普通完成消息可能丢失，生产协议应让查询返回保留结果或使用应用确认。

## 保持工作与回复生命周期分离

handler 的请求 view 只在调用中有效，`pattern` 在接纳 job 时复制到应用状态。不要保存响应 scratch 以便稍后返回。重复 Start 可以再次执行；本地 cancel/timeout 不撤销任务。幂等接纳、持久 job 身份和关闭时的设备动作由应用决定。普通发送 `WL_OK` 只表示 snapshot 接纳，物理完成仍由 adapter 推进。

更多业务组合与扩展检查见[设备服务](../examples/03_device_service/README-cn.md)，传输接入见[一个 owner loop](tutorial-integration-cn.md)。
