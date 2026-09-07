# 进阶：不阻塞的 RPC 客户端

如果程序还要处理按键、显示或其他任务，可以发起请求后立即返回，完成时再接收通知。
服务端和消息定义仍用[加法示例](tutorial-rpc-cn.md)，不需要换一种 RPC 协议。
[English](tutorial-rpc-async.md)。

## 1. 运行

先启动 `calculator_server`，再运行同目录的 `calculator_client_async`。
构建及端口参数与同步客户端相同，正常输出仍是 `20 + 22 = 42`。

## 2. 完整客户端

[client_async.c](../examples/01_rpc/client_async.c)：

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

  CHECK(calculator_endpoint_init(&client, wl_platform_environment()) == WL_OK);
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

`_async()` 返回 `WL_OK` 表示已复制请求并接受，不表示远端成功。
队列满等提交错误不会触发回调；已接受的调用在持续驱动或有序关闭中恰好通知一次。
完成时框架自动回收调用，业务只接收一个最终结果，不需要 inspect/release。

`addition_t` 是本程序选择保存的业务结果，不是用户拼装的端点或协议缓冲区。
完成回调的 result/response 指针不能带出回调；复制它们的值即可保存，
包括[有界字符串](tutorial-rpc-values-cn.md)。非成功结果的 response 为 NULL。

## 3. 谁继续推进通信

示例主循环调用 step，再通过 UDP 就绪等待休眠，不靠忙轮询消耗 CPU。
实际 UI/RTOS 可把推进接入自己的事件循环，详见[平台集成](rpc-platform-cn.md)。
不要让两个线程同时 step，也不要在同一端点的回调中递归 step、sync 或 close。

回调可以提交下一次 async 或取消其他调用。想停止时先设置业务标志，返回 owner
主循环后调用生成的 close；Ctrl+C 路径也会先通知在途调用，再关闭 UDP。
需要取消时把提交的最后一个 NULL 换成 `wl_rpc_call_t` 的地址，再调用
`calculator_endpoint_cancel()`。超时和取消都不撤销远端已发生的副作用。

服务端本身也有慢任务？继续阅读[延迟回复](tutorial-rpc-deferred-cn.md)。

