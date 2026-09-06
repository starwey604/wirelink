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

Wirelink 自动给每次调用分配内部编号并匹配响应，应用不需要保存或比较它。
普通接口在完成时通知应用并回收调用资源；只有需要主动取消时才领取可选句柄。

## 4. 客户端：提交参数，完成时接收结果

完整 [client.c](../examples/01_rpc/client.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "tutorial_host.h"

typedef struct {
  bool done;
  wl_rpc_completion_t result;
  add_response_value_t response;
} addition_t;

static void completed(void *context, const wl_rpc_completion_t *result,
                       const add_response_value_t *response) {
  addition_t *addition = context;
  addition->result = *result;
  if (response != NULL) addition->response = *response;
  addition->done = true;
}

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  addition_t addition = {0};
  add_request_value_t request;
  uint16_t local = 49100, peer = 49101;
  CHECK(argc == 1 || argc == 3 || argc == 5);
  CHECK(example_ports(argc == 5 ? 3 : argc, argv, &local, &peer));
  add_request_value_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  if (argc == 5) {
    CHECK(example_int32(argv[3], &request.left));
    CHECK(example_int32(argv[4], &request.right));
  }

  CHECK(calculator_endpoint_init(&client, example_session_id(), example_clock()) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);
  CHECK(calculator_endpoint_add_async(&client, &request, 1500U,
      completed, &addition, NULL) == WL_OK);

  while (!addition.done && example_running()) {
    const int step = calculator_endpoint_step(&client);
    if (step != WL_OK) fprintf(stderr, "endpoint: %s\n", wl_err_str(step));
    if (!addition.done) CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&client) == WL_OK); /* Also completes a call interrupted by Ctrl-C. */
  if (addition.result.status == WL_RPC_SUCCESS) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)addition.response.sum);
  } else if (addition.result.status == WL_RPC_REJECTED) {
    printf("addition rejected: status=%ld\n", (long)addition.result.rejection);
  } else {
    fprintf(stderr, "RPC failed: %s\n", wl_rpc_status_str(addition.result.status));
  }
  example_udp_close(udp);
  return addition.result.status == WL_RPC_SUCCESS ||
         addition.result.status == WL_RPC_REJECTED ? 0 : 1;
}
```

先只看三处：

1. `add_request_value_t` 是生成的业务值。填好输入后调用 `endpoint_add_async()`；
   `1500U` 是从接受请求起计算的等待上限，包含本地排队时间。
2. `completed()` 在调用结束时收到结果。成功时有响应，失败时响应指针为 NULL。
   这里把结果复制进程序自己的 `addition`，所以退出回调、关闭端点后仍能打印。
3. 主循环只负责推进通信、等待 socket 唤醒，直到完成或 Ctrl+C。
   不再检查链路中间状态，也不需要 `release`。线程式的一行同步调用属于后续平台接口，
   当前例子展示的是单执行者异步接口。

`_async()` 返回 `WL_OK` 只表示“已快照请求并接受调用”，不表示计算成功。
此后可以改写请求变量。返回 `WL_ERR_BUSY` 表示本地固定容量已满，本次没有接受、
也不会回调；应用可稍后再提交。其他提交错误同样不会回调。

初始化时提供时钟；首次调用不需要先 `step()`。正常推进中的完成回调不额外读取时钟。
`example_udp_*` 是[示例平台支持](../examples/common/tutorial_host.h)，不是另一套 RPC 实现。

## 5. 服务端：填响应或返回业务拒绝码

完整 [server.c](../examples/01_rpc/server.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_endpoint.h"
#include "tutorial_host.h"

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  const int64_t sum = (int64_t)request->left + request->right;
  (void)context;
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  if (sum < INT32_MIN || sum > INT32_MAX)
    return 1; /* Business rejection; framework errors are separate. */
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  config.clock = example_clock();
  config.on_add = add;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&server) == WL_OK);
  example_udp_close(udp);
  return 0;
}
```

`config.on_add = add` 注册服务。框架负责准备请求、清空响应、调用函数并发送结果。
handler 不需要知道 endpoint、token 或 delivery；`context` 仅用于自己的业务状态，
需要时通过 `config.add_user_data` 提供。

返回 0 表示成功，必须填写响应的 required 字段；非零值仅表示协议双方约定的业务拒绝码。
例如这里用 1 表示加法越界。不要返回 `WL_ERR_*` 充当框架错误——它们会被当作业务拒绝码。
响应编码错误、发送错误等由框架诊断路径报告，不与拒绝码混用。
不要在这个即时 handler 中等待电机完成或执行长时间阻塞工作；延迟回复有独立的
[高级 token 接口](rpc-runtime-cn.md)。

## 6. 正常运行时需要记住的边界

- 完成只有成功、业务拒绝、超时、取消、通信失败五种结果；
  `wl_rpc_completion_t` 的诊断属于本次调用，不使用共享 last_error。
- 已接受请求在持续推进或有序关闭时恰好回调一次。回调中的响应指针只在该回调内有效；
  复制 `*response` 得到自持副本，包括有界 string/bytes，无需析构。
- 回调可以提交或取消其他调用；不能递归 `step` 或在回调中同步 `close` 同一端点。
  设置应用的停止标志，在回到主循环后关闭。关闭会结束尚未通知的调用。
- 取消句柄是可选的：将最后一个 NULL 换成 `&call`（类型 `wl_rpc_call_t`），
  然后调用 `calculator_endpoint_cancel(&client, &call)`。仍由完成回调通知结果。
  超时或取消不撤销远端已经发生的副作用。
- 默认四个 RPC 槽，单个链路发送槽。框架有界排队，不创建线程、堆或无限队列。
  默认缓存只保留有限的最近结果，允许淘汰已送达的最旧响应；
  10 秒 TTL 是最长保留时间，不是“10 秒内绝不重复执行”的保证。
  调优和严格缓存策略见[默认端点](default-endpoint-cn.md)。

现在可以试着改变左右参数，或运行
`calculator_client 49100 49101 2147483647 1` 观察业务拒绝。
重复运行客户端无需等待缓存 TTL。UDP 丢失下的确认与重传由 Wirelink 处理，
但 UDP 示例不提供身份认证或加密，不应直接暴露到不可信网络。

下一篇：[把端点接入自己的程序](tutorial-integration-cn.md)。
