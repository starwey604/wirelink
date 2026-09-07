# 进阶：服务不能立即完成时

即时 handler 适合读配置、查询状态和短计算。电机运动、Flash 操作或工作线程上的任务，
不能在 handler 内一直等待，否则同一端点的接收、ACK 和其他调用也停了。
只有这时才需要延迟回复。客户端仍可自行选择 sync 或 async。
[English](tutorial-rpc-deferred.md)。

## 1. 运行独立的延迟服务端

用 `calculator_server_deferred` 替代 `calculator_server`，不要同时占用相同端口。
两种客户端都能连接它，schema/profile 不变。

这个小示例把加法结果暂存，在下一次 owner 循环回复，不模拟电机，也不创建线程。
它演示的是“接下任务”和“回复结果”分开；完整
[server_deferred.c](../examples/01_rpc/server_deferred.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_advanced.h"
#include "tutorial_host.h"

typedef struct {
  calculator_endpoint_t *server;
  bool pending;
  calculator_add_request_token_t token;
  int64_t sum;
} job_t;

static int32_t enqueue(void *context, const add_request_t *request,
                       const calculator_add_request_token_t *token,
                       wl_delivery_t delivery) {
  job_t *job = context;
  (void)delivery;
  if (job->pending)
    return calculator_endpoint_add_reject(job->server, token, 2);
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  job->sum = (int64_t)request->left + request->right;
  job->token = *token;
  job->pending = true;
  return 0; /* Accepted locally, not a completed RPC response. */
}

/* The demo job becomes ready on the next owner-loop pass. A real worker
 * publishes a result and wakes this owner; it must not call complete itself. */
static wl_rpc_err_t finish(job_t *job) {
  add_response_t response;
  job->pending = false;
  if (job->sum < INT32_MIN || job->sum > INT32_MAX)
    return calculator_endpoint_add_reject(job->server, &job->token, 1);
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)job->sum;
  return calculator_endpoint_add_complete(job->server, &job->token, &response);
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  job_t job = {0};
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  job.server = &server;
  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  config.clock = example_clock();
  config.advanced.add_request_handler = enqueue;
  config.advanced.add_user_data = &job;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready (deferred)");
  fflush(stdout);
  while (example_running()) {
    if (job.pending) CHECK(finish(&job) == WL_RPC_OK);
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    if (!job.pending) CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&server) == WL_OK);
  job.pending = false; /* A saved token cannot outlive its endpoint. */
  example_udp_close(udp);
  return 0;
}
```

## 2. 多出来的概念各负责什么

- `calculator_advanced.h` 是主动选择的高级入口；同一服务不能同时注册即时和延迟 handler。
- token 是框架交付的回复凭据。复制整体以供稍后使用，不拆字段，也不自己构造调用编号。
- `job_t` 是本例的一项业务任务，不是 Wirelink 要求的公共端点结构体。
  它只保存计算结果和 token，不保存回调 request 指针。
- 高级 handler 返回 0 表示本地接手，**不会自动回复成功**。
  用 complete 回复成功，reject 回复非零业务拒绝码；本例 1 表示溢出，2 表示业务工作位忙。
  返回非零本身是本地放弃，不会自动变成业务拒绝包。
- complete 把响应存入框架，后续 step 负责发出；complete 成功不等于对端已经收到。

高级请求/响应使用 codec 视图，string/bytes 可能借用输入。
延后处理时复制所需字段，或显式使用生成的 value-from-view 转换；
不要把含借用指针的结构体浅复制当作数据快照。
普通即时 handler 仍使用自持值，不受这个高级选择影响。

## 3. 接入真正的慢任务

worker 只计算并发布业务结果，然后唤醒原 owner；由 owner 调用 complete/reject。
不要从 worker 直接操作 endpoint。一次示例工作位只是应用容量，不能替代框架的 RPC 容量。

本例任务在下一轮即完成。真实慢任务要给 pending deadline 和应用任务设置合理边界；
客户端可能已经超时，token 也可能因过期、换会话或关闭失效。
无效 token 不能拿新编号“补发”；业务应丢弃过期回复并按自身策略结束任务。
关闭前停止/排空 worker，关闭后丢弃 token，不能引用已销毁端点。
超时或本地取消并不保证远端动作停止。

详细错误与生命周期约定见[高级 RPC runtime](rpc-runtime-cn.md)。

