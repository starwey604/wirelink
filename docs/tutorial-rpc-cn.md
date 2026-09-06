# 第二篇：请求设备计算加法

上一组[遥测示例](getting-started-cn.md)只发送测量值。这次控制端想问：
“请计算 20 + 22，并告诉我结果。”这种发起操作、等待对端结果的交互叫
**RPC（远程过程调用）**。

[`01_rpc`](../examples/01_rpc/) 是独立的加法示例，不包含遥测。
`calculator_client` 发起请求，`calculator_server` 处理并回复。
两个程序通过本机 UDP 通信；业务逻辑仍调用 Wirelink，而不是直接读写 socket。
[English](tutorial-rpc.md)。

## 1. 先运行

使用上一篇的构建目录；若更新了源码，先按[安装篇](installation-cn.md)更新 WLC 并重新构建：

```sh
cmake --build build/tutorials --config Release --parallel
```

终端 A：

```sh
./build/tutorials/examples/01_rpc/calculator_server
```

看到 `calculator server ready` 后，在终端 B：

```sh
./build/tutorials/examples/01_rpc/calculator_client
```

客户端输出 `20 + 22 = 42` 后退出，服务端保持运行，可按 Ctrl+C 退出。
Windows 多配置构建的可执行文件放在 `01_rpc/Release/` 下，文件名带 `.exe`。

## 2. 消息只定义业务参数

完整 [`calculator.wl`](../examples/01_rpc/calculator.wl)：

```text
version 1;

message AddRequest @id(20) {
  required int32 left @id(1);
  required int32 right @id(2);
}

message AddResponse @id(21) {
  required int32 sum @id(1);
}
```

`AddRequest` 包含输入，`AddResponse` 包含输出。`@id(20)` 和 `@id(21)`
区分消息类型，不是本次调用的编号。消息里不必预留 Wirelink 的编号或状态字段。

文件名 `calculator.wl` 给生成端点提供业务名称：`calculator_endpoint_t`。
目录中的 `01` 只是阅读顺序，不进入 C API。

## 3. 将输入和输出组成 RPC 服务

完整 [`calculator.bind.wl`](../examples/01_rpc/calculator.bind.wl)：

```text
profile version 1;

rpc Add {
  request = AddRequest;
  response = AddResponse;
}
```

`rpc Add` 是服务名称，`request`、`response` 指定输入、输出类型。
**RPC 请求和响应默认都采用可靠传输。** 这里的可靠性由 Wirelink 提供，UDP 本身不重传。
有特殊需要才在对应方向写属性，例如：

```text
rpc Add {
  request = AddRequest @delivery(unreliable);
  response = AddResponse;
}
```

这个覆盖只影响请求方向，响应仍可靠。属性属于消息的使用配置，不属于消息定义。
默认可靠不代表无限重试、没有超时或业务只执行一次；重试次数与等待时间仍由运行配置选择。

Wirelink 自动给调用分配内部编号，随请求发送、随响应带回并匹配。
应用保存返回的 **调用句柄**，就像领取这次结果的凭据，不需要比较或修改内部编号。
这就是默认的托管 RPC。

## 4. 客户端：提交参数，等待结果

完整 [`client.c`](../examples/01_rpc/client.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_runtime.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  calculator_endpoint_config_t config;
  calculator_add_call_t call;
  calculator_add_result_t result;
  add_request_t request;
  uint16_t local = 49100, peer = 49101;
  CHECK(argc == 1 || argc == 3 || argc == 5);
  CHECK(example_ports(argc == 5 ? 3 : argc, argv, &local, &peer));
  add_request_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  if (argc == 5) {
    CHECK(example_int32(argv[3], &request.left));
    CHECK(example_int32(argv[4], &request.right));
  }

  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  config.clock = example_clock();
  CHECK(calculator_runtime_config_enable_client(&config.runtime) == WL_OK);
  config.link.ack_timeout_ms = 100U;
  config.link.max_retries = 4U;
  CHECK(calculator_endpoint_init_config(&client, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);
  CHECK(calculator_endpoint_add_call(&client, &request, 1500U, &call) == WL_RPC_OK);

  for (;;) {
    const int step = calculator_endpoint_step(&client);
    if (step != WL_OK) fprintf(stderr, "endpoint: %s\n", wl_err_str(step));
    CHECK(calculator_endpoint_add_inspect(&client, &call, &result) == WL_RPC_OK);
    if (result.state == WL_RPC_CLIENT_COMPLETED ||
        result.state == WL_RPC_CLIENT_APPLICATION_ERROR ||
        result.state == WL_RPC_CLIENT_TIMED_OUT ||
        result.state == WL_RPC_CLIENT_LINK_FAILED ||
        result.state == WL_RPC_CLIENT_CANCELLED) break;
    if (!example_running()) CHECK(calculator_endpoint_add_cancel(&client, &call) == WL_RPC_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  if (result.state == WL_RPC_CLIENT_COMPLETED && result.response_valid) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)result.response.sum);
  } else if (result.state == WL_RPC_CLIENT_APPLICATION_ERROR) {
    printf("addition rejected: status=%ld\n", (long)result.application_status);
  } else {
    fprintf(stderr, "RPC failed: state=%ld\n", (long)result.state);
  }
  CHECK(calculator_endpoint_add_release(&client, &call) == WL_RPC_OK);
  example_udp_close(udp);
  return result.state == WL_RPC_CLIENT_COMPLETED ||
         result.state == WL_RPC_CLIENT_APPLICATION_ERROR ? 0 : 1;
}
```

按业务顺序读即可：

1. 填写 `left`、`right` 和对应 `has_...` 标志。
2. 配置客户端角色；本例链路确认等待 100 毫秒，最多重传 4 次。
3. `endpoint_add_call(..., 1500U, &call)` 提交调用，应用响应等待上限为 1500 毫秒。
   返回 `WL_RPC_OK` 表示提交成功，不表示服务端已经算完。
   初始化时设置 `config.clock = example_clock()`；提交时内部取一次时间，同时用于
   链路重传与 RPC 截止时间。首次提交前不需要额外调用 `step()`。
4. 持续 `endpoint_step()`，用 `endpoint_add_inspect()` 查询。
   查询成功与调用成功不同；查看 `result.state`，成功时还应检查 `response_valid`。
5. 用完结果后 `endpoint_add_release()`，失败、取消和超时也要释放。
   关闭端点或释放调用后，旧句柄不能再用。

`example_udp_*` 和时钟函数仍是上一篇介绍的[公共平台支持代码](../examples/common/tutorial_host.h)，
不是另一套 RPC 实现。正常等待通过 socket 唤醒，不需要每毫秒空转一次。

## 5. 服务端：只关注如何计算和回复

完整 [`server.c`](../examples/01_rpc/server.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_runtime.h"
#include "tutorial_host.h"

static int32_t add(void *context, const add_request_t *request,
                   const calculator_add_request_token_t *token, wl_delivery_t delivery) {
  calculator_endpoint_t *server = context;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  if (sum < INT32_MIN || sum > INT32_MAX)
    return calculator_endpoint_add_reject(server, token, 1);
  add_response_t response;
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  return calculator_endpoint_add_complete(server, token, &response);
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  config.clock = example_clock();
  CHECK(calculator_runtime_config_enable_server(&config.runtime) == WL_OK);
  config.link.ack_timeout_ms = 100U;
  config.link.max_retries = 4U;
  config.runtime.rpc_server_pending_timeout_ms = 1000U;
  config.runtime.rpc_server_cache_ttl_ms = 10000U;
  config.runtime.add_request_handler = add;
  config.runtime.add_user_data = &server;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  example_udp_close(udp);
  return 0;
}
```

服务端配置 `add_request_handler` 后，推进端点时就会收到处理回调。
`context` 是我们传入的服务端指针，不包含手工组装的通信缓冲区。

`calculator_add_request_token_t` 是回复当前请求的凭据。用它调用
`endpoint_add_complete()` 准备成功响应，之后由端点推进发送。
无需从请求中提取编号、再把编号写入响应。

处理函数返回零表示本地正常接手，不要求已经发送完成。
慢任务可以复制需要的参数和 token，稍后在同一个通信处理线程上回复。
返回非零是本地处理失败，**不会自动发送业务拒绝**。

## 6. 拒绝、超时和重复请求

加法先用 64 位整数计算，避免有符号溢出。结果超出 32 位范围时，
`endpoint_add_reject(..., 1)` 返回本例约定的拒绝状态 1，不必伪造一个 `sum`。
试着运行：

```sh
./build/tutorials/examples/01_rpc/calculator_client 49100 49101 2147483647 1
```

客户端应输出 `addition rejected: status=1`。`response_valid` 为假，
`application_status` 才是拒绝原因。

UDP 包丢失时，Wirelink 可靠传输可以重传；缓存有效期内重复请求不会重复执行加法。
但链路 ACK 只说明链路接收，不说明业务已经完成。
**取消或超时都不保证对端没执行，也不会远程撤销操作。**
相同参数重新 `call()` 是新操作；扣款、运动等业务要另行设计幂等或状态查询。

默认端点只有一个客户端调用位置，释放后再发下一次；高级存储支持多个并发调用。
调用编号未复用时，已取消、超时或释放的调用收到迟到响应，只留下诊断。
跨客户端重建、编号复用后的旧响应隔离仍有限制，见 [RPC 合同](rpc-runtime-cn.md)。

## 下一步

两个程序默认固定互为对端，端口分别为 49100、49101。
服务端不是多客户端网络服务器；程序重启后仍需遵守
[会话和旧流量隔离规则](tutorial-integration-cn.md#session-identity)。

继续读[工程集成](tutorial-integration-cn.md)，了解安装包、自定义平台与存储。
原来的字段映射方式仍支持，旧 `request_delivery = ...` 写法也兼容；
同一方向重复声明属性和旧属性会报错，不会静默覆盖。
