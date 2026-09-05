# 第二篇：请求设备完成一次计算

上一篇[最新温度显示](getting-started-cn.md)只发送测量值。现在控制端想问设备：
“请计算 20 + 22，并告诉我结果。”这种发起操作、等待对端结果的交互叫
**RPC（远程过程调用）**。本篇继续用内存连接，无需开发板。[English](tutorial-rpc.md)。

## 1. 先运行

使用上一篇的构建目录；如果更新过源码，先按[安装篇](installation-cn.md)换成匹配的 WLC：

```sh
cmake --build build/quickstart --target wirelink_getting_started
./build/quickstart/examples/wirelink_getting_started
```

预期输出：

```text
unreliable telemetry: sample=7 temperature=23.50 C
reliable RPC: 20 + 22 = 42
```

程序先发送一次温度，然后请求设备计算加法。接下来只关注这次计算。

## 2. 消息只描述业务参数

完整 [`quickstart.wl`](../examples/getting_started/quickstart.wl)：

```text
version 1;

message Telemetry @id(10) {
  required uint32 sample @id(1);
  required int32 temperature_centi_c @id(2);
}

message AddRequest @id(20) {
  required int32 left @id(1);
  required int32 right @id(2);
}

message AddResponse @id(21) {
  required int32 sum @id(1);
}
```

`AddRequest` 的两个加数是输入，`AddResponse` 的 `sum` 是输出。
`@id(20)` 和 `@id(21)` 区分请求、响应这两种消息，不代表第几次调用。
请求中没有调用编号，响应中也不需要为 Wirelink 预留编号或状态字段。

## 3. 告诉 Wirelink 哪两个消息构成一次调用

完整 [`quickstart.bind.wl`](../examples/getting_started/quickstart.bind.wl)：

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}

rpc Add {
  request = AddRequest;
  response = AddResponse;
  request_delivery = reliable;
  response_delivery = reliable;
}
```

`rpc Add` 给服务起名，`request` 和 `response` 指定输入、输出类型。
最后两行选择它们的传输方式；本例都使用可靠传输。

Wirelink 自动为每次调用分配内部编号、随请求发送、随响应带回并进行匹配。
这些信息由 RPC 管理，不需要出现在业务结构体里。我们把这种默认方式称为**托管 RPC**。
普通业务代码只保存发起调用时返回的句柄，稍后用它查看结果。句柄可以理解为
“这次调用的领取凭据”：不要读取或修改其内部成员。

## 4. 完整程序

两个端点仍由 WLC 生成。本次通过配置启用客户端、服务端角色，并给服务端指定加法处理函数。
`calculator_t` 只是本例的业务上下文，用来传递设备端点和模拟时刻，不包含通信缓冲区。
`CHECK` 是示例错误检查宏，失败就结束电脑进程，不是 Wirelink API。

下面是完整 [`examples/getting_started.c`](../examples/getting_started.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "quickstart_runtime.h"
#include "wirelink/loopback.h"

#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
    return 1; \
  } \
} while (0)

typedef struct {
  quickstart_endpoint_t *device;
  wl_time_ms_t now_ms;
} calculator_t;

static int32_t handle_add(void *context, const add_request_t *request,
                          const quickstart_add_request_token_t *token,
                          wl_delivery_t delivery) {
  calculator_t *calculator = context;
  add_response_t response;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  if (sum < INT32_MIN || sum > INT32_MAX) {
    /* Business rejection: no fabricated sum or status field is needed. */
    return quickstart_endpoint_add_reject(calculator->device, token, 1,
                                          calculator->now_ms);
  }
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  /* Preparing a response is synchronous; endpoint_step sends it later. */
  return quickstart_endpoint_add_complete(calculator->device, token,
                                          &response, calculator->now_ms);
}

int main(void) {
  static quickstart_endpoint_t controller, device;
  quickstart_endpoint_config_t client_config, server_config;
  calculator_t calculator = {&device, 0U};
  wl_loopback_t cable;
  telemetry_t telemetry, received;
  add_request_t request;
  quickstart_add_result_t operation;
  quickstart_add_call_t call;

  CHECK(quickstart_endpoint_config_defaults(&client_config, 0x1001U) == WL_OK);
  CHECK(quickstart_endpoint_config_defaults(&server_config, 0x2002U) == WL_OK);
  CHECK(quickstart_runtime_config_enable_client(&client_config.runtime) == WL_OK);
  CHECK(quickstart_runtime_config_enable_server(&server_config.runtime) == WL_OK);
  client_config.link.ack_timeout_ms = 20U;
  client_config.link.max_retries = 2U;
  server_config.link.ack_timeout_ms = 20U;
  server_config.link.max_retries = 2U;
  server_config.runtime.rpc_server_pending_timeout_ms = 1000U;
  server_config.runtime.rpc_server_cache_ttl_ms = 10000U;
  server_config.runtime.add_request_handler = handle_add;
  server_config.runtime.add_user_data = &calculator;
  CHECK(quickstart_endpoint_init_config(&controller, &client_config) == WL_OK);
  CHECK(quickstart_endpoint_init_config(&device, &server_config) == WL_OK);
  CHECK(wl_loopback_connect(&cable, quickstart_endpoint_handle(&controller),
                           quickstart_endpoint_handle(&device)) == WL_OK);

  telemetry_clear(&telemetry);
  telemetry.has_sample = true;
  telemetry.sample = 7U;
  telemetry.has_temperature_centi_c = true;
  telemetry.temperature_centi_c = 2350;
  CHECK(quickstart_endpoint_send_telemetry(&device, &telemetry).domain == QUICKSTART_SEND_OK);
  CHECK(quickstart_endpoint_step(&device, 1U) == WL_OK);
  CHECK(quickstart_endpoint_step(&controller, 1U) == WL_OK);
  CHECK(quickstart_endpoint_read_telemetry(&controller, &received) == WL_OK);
  CHECK(received.sample == 7U && received.temperature_centi_c == 2350);

  add_request_clear(&request);
  request.has_left = true;
  request.left = 20;
  request.has_right = true;
  request.right = 22;
  CHECK(quickstart_endpoint_add_call(&controller, &request, 100U, 10U,
                                    &call) == WL_RPC_OK);

  /* Simulated milliseconds. Real applications use their monotonic clock. */
  for (calculator.now_ms = 10U; calculator.now_ms < 30U; ++calculator.now_ms) {
    CHECK(quickstart_endpoint_step(&controller, calculator.now_ms) == WL_OK);
    CHECK(quickstart_endpoint_step(&device, calculator.now_ms) == WL_OK);
  }
  CHECK(quickstart_endpoint_add_inspect(&controller, &call, &operation) == WL_RPC_OK);
  CHECK(operation.state == WL_RPC_CLIENT_COMPLETED);
  CHECK(operation.response_valid && operation.response.sum == 42);
  CHECK(quickstart_endpoint_add_release(&controller, &call) == WL_RPC_OK);

  quickstart_endpoint_close(&controller);
  quickstart_endpoint_close(&device);
  puts("unreliable telemetry: sample=7 temperature=23.50 C");
  puts("reliable RPC: 20 + 22 = 42");
  return 0;
}
```

## 5. 顺着一次调用阅读

1. 控制端只填写 `left`、`right` 及对应的 `has_...` 标志，然后调用
   `quickstart_endpoint_add_call(..., 100U, 10U, &call)`。100 是等待上限（毫秒），
   10 是当前时刻；成功表示请求已在本地提交，`call` 用于后续查结果。
2. 两端持续调用 `endpoint_step()`。设备收到请求后，Wirelink 调用 `handle_add()`。
   函数计算结果，再用 `endpoint_add_complete()` 准备响应；后续推进负责发送。
3. 控制端用 `endpoint_add_inspect(&controller, &call, &operation)` 查看状态。
   查询返回 `WL_RPC_OK` 只表示查询成功；`operation.state` 才说明调用是否完成。
   成功完成且 `response_valid` 为真时，直接读取 `operation.response.sum`。
4. 不再需要结果时调用 `endpoint_add_release()` 回收位置。失败、取消和超时等终态也要释放。
   释放后不能再使用旧句柄；关闭、重新初始化端点也会让旧句柄失效。

这里用循环变量模拟单调递增的毫秒时钟；真实程序持续在同一个通信线程或主循环中推进端点。
示例配置的会话标识 `0x1001`、`0x2002` 只适用于隔离模拟。
设备重启时如何选择新标识，放在[集成篇](tutorial-integration-cn.md#session-identity)说明。

默认静态端点为客户端保留一个调用位置：上一次调用释放后再发下一次。
库的 RPC 引擎支持多调用并存；需要更多位置时使用集成篇的自定义存储，
而不是让业务自己分配编号。多个调用的回复可以乱序，库按调用匹配。

## 6. 算不出来、等不到结果，分别怎么办

本例先用 64 位整数计算，避免两个 32 位加数相加发生溢出。
超出结果范围时，处理函数调用 `endpoint_add_reject(..., 1, now)`：
这里的 1 是本例约定的“结果超出范围”，不是 Wirelink 的编号。
控制端会得到 `WL_RPC_CLIENT_APPLICATION_ERROR`、`application_status == 1`，
以及 `response_valid == false`；不需要制造一个无意义的 `sum`。

处理函数返回 0 表示本地处理正常，既可以已经回复，也可以把工作留待稍后完成。
返回非零表示本地处理失败，不会自动替你回复业务拒绝。
慢任务应复制需要的业务参数和 `quickstart_add_request_token_t`，工作结束后在通信 owner 上
用同一个 token 调用 `complete()` 或 `reject()`；token 本身不需要拆解。

可靠传输的 ACK 只证明链路接收，不证明设备已经算完。没有收到应用响应而超过等待上限时，
调用进入 `WL_RPC_CLIENT_TIMED_OUT`；主动调用 `endpoint_add_cancel()` 则进入取消状态。
**取消或超时不保证设备没有执行，也不会远程撤销业务操作。** 是否重试由业务决定。
对于扣款、运动等不能重复执行的操作，要另外设计业务幂等或状态查询。

对于已经取消、超时或释放且内部编号未复用的调用，迟到响应会被忽略并留下诊断。
需要查看异常包或迟到回复时可以配置诊断回调，但正常应用不需要自己比较内部编号。
跨客户端重建时的旧响应隔离仍有限制，见 [RPC 合同](rpc-runtime-cn.md)。

## 下一步

把加数改成 `INT32_MAX` 和 `1`，并把结果检查改成上述拒绝状态，观察失败路径。
然后阅读[集成篇](tutorial-integration-cn.md)，了解如何接到真实串口、配置存储和调度。

已有协议使用 `operation_id` 字段映射时，仍可选择兼容方式；它不是默认入门 API。
两种 RPC 格式不能直接互通，升级需同步通信双方。
字段映射、12 字节 RPC 元数据以及重放保证的边界见 [RPC 合同](rpc-runtime-cn.md)。
