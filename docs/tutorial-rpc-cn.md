# 第二篇：请求设备完成一次计算

上一篇[最新温度显示](getting-started-cn.md)只有单向更新。现在控制端要问设备：
“请计算 20 + 22，并把结果告诉我。”控制端需要知道结果属于哪次请求，
也需要在设备没回应时结束等待。这就是 **RPC（远程过程调用）**：
在连接另一端请求一次操作，并等待它的应用层结果。

本篇继续使用内存连接。完整示例还保留上一篇的温度发送，然后完成一次加法。
[English](tutorial-rpc.md)。

## 1. 先分清“送到了”和“算完了”

可靠传输会让接收方发回 **ACK（接收确认）**。如果确认迟迟没到，
链路可在配置允许的次数内重发。ACK 证明的是链路接收，不是业务执行成功。

RPC 另外发送一条响应：例如“这次计算成功，结果是 42”。
因此本例有请求、请求的 ACK、响应、响应的 ACK。
请求和响应均选可靠传输，但 RPC 与可靠传输不是同一个概念。

## 2. 增加请求和响应消息

完整 [`quickstart.wl`](../examples/getting_started/quickstart.wl)：

```text
version 1;

enum AddStatus = 1 {
  ADD_OK = 0;
  ADD_REJECTED = 1;
}

message Telemetry = 10 {
  required uint32 sample = 1;
  required int32 temperature_centi_c = 2;
}

message AddRequest = 20 {
  optional uint32 operation_id = 1;
  required int32 left = 2;
  required int32 right = 3;
}

message AddResponse = 21 {
  optional uint32 operation_id = 1;
  optional AddStatus status = 2;
  required int32 sum = 3;
}
```

`AddRequest` 携带两个加数，`AddResponse` 携带结果。
消息编号 20 和 21 标识两种消息；它们不标识某一次计算。
`AddStatus` 是业务状态枚举，0 表示成功，1 表示应用拒绝。
本例只演示成功路径。

同一时刻可能有多次调用，因此还需要一个**调用编号**：
请求带编号 7，响应也带编号 7，控制端才能把这份结果交给正确的调用。
这就是本例的 `operation_id`。

这些元数据在 schema 中标为 `optional`，方便应用只填写业务参数。
生成的 RPC 启动函数会补入调用编号，完成函数会补入对应编号及成功状态。
这不表示收到的 RPC 消息可以缺少它们：RPC 接收路径会检查编号非零，
响应还必须有状态字段。普通编码器和 RPC 检查的是不同层次的约束。

## 3. 将两个消息组合成一个 RPC

完整 [`quickstart.bind.wl`](../examples/getting_started/quickstart.bind.wl)：

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}

rpc Add {
  request = AddRequest;
  response = AddResponse;
  request_operation_id = operation_id;
  response_operation_id = operation_id;
  response_status = status;
  request_delivery = reliable;
  response_delivery = reliable;
}
```

`rpc Add` 给服务起名 `Add`；`request`、`response` 指定它使用的消息类型；
最后两行选择请求和响应各自的传输方式。

中间三行是**字段映射，不是运行时赋值**：

| 配置项 | 右侧名称在哪个消息里查找 | 意义 |
| --- | --- | --- |
| `request_operation_id = operation_id` | `AddRequest` | 从这个字段获取请求的调用编号 |
| `response_operation_id = operation_id` | `AddResponse` | 从这个字段获取响应对应的调用编号 |
| `response_status = status` | `AddResponse` | 从这个字段获取业务执行状态 |

**两边可以“不等”吗？需要区分字段名和字段值。**

字段名可以不同。例如接入已有协议，请求中的编号已经叫 `request_id`，
响应中叫 `reply_to`，就可以写：

```text
request_operation_id = request_id;
response_operation_id = reply_to;
```

前提是消息定义中确实有这些字段，且调用编号字段是非重复的 `uint32`。
请求和响应是不同消息，字段编号也可以不同。
分开映射允许复用这样的消息定义，不强迫每个项目使用同一个字段名。

但同一次调用的**数值必须相同**。请求的 `request_id = 7`，
响应就应为 `reply_to = 7`。如果响应是 8，它不能完成编号 7 的调用；
是否匹配另一条调用或被拒绝，取决于当前有哪些待处理调用。

新项目统一叫 `operation_id` 最直观。当前语法仍要求显式写出两个映射。
普通使用生成的 `client_start()` / `server_complete()` 时，
应用无需自己复制这个编号。

## 4. 为可靠通信认识一个新概念：重启后的身份

想象设备重启了，但连接里还残留着重启前的包或确认。
如果新旧包只靠容易重新从头计数的序号来区分，就可能把旧确认当成新请求的确认。

`session_id` 用来标识“这是本次启动或本次通信实例的流量”。
可靠数据包和确认包携带相关会话标识，使协议能区分旧会话的流量。
**非零**只是数值不能为 0，因为 0 被接口保留为无效值。

它不是用来选择“把包发给哪个设备”的地址。Wirelink 连接的是两端，
本身不提供按节点地址寻路。本例用 0x1001、0x2002 表示两个隔离的模拟端，
方便输出和测试可重复；真实设备不能每次重启都照抄这些固定值。

一种做法是每次启动生成一个新的非零随机数，常称为 **boot nonce**，
也就是“这次启动使用的随机标识”；另一种做法是在持久存储中维护启动计数，
每次启动先递增再使用。目标都是避免旧包仍可能存在时复用同一个标识。
随机方案还要考虑碰撞概率；它不是认证密码或加密密钥。

## 5. 运行示例，再按业务过程阅读

沿用上一篇已经配置的构建目录：

```sh
cmake --build build/quickstart --target wirelink_getting_started
./build/quickstart/examples/wirelink_getting_started
```

```text
unreliable telemetry: sample=7 temperature=23.50 C
reliable RPC: 20 + 22 = 42
```

两个端点仍是生成的 `quickstart_endpoint_t`。这次用
`endpoint_config_defaults()` / `endpoint_init_config()` 设置额外策略：
控制端启用 client（发起请求），设备端启用 server（接收请求）。
`config.link` 配置连接，`config.runtime` 配置消息处理；它们只是初始化参数，
不是需要独立驱动的对象。端点仍自动管理存储和通信推进。

一次调用的流程是：

1. 控制端填写两个加数，调用 `quickstart_endpoint_add_start(..., 100U, 10U)`。
   100 是最多等待 100 毫秒，10 是当前毫秒时刻；保存返回的调用编号。
2. 设备推进通信后调用 `handle_add()`。本例计算很快，所以回调中直接算出结果，
   然后调用 `quickstart_endpoint_add_complete()` 准备响应。
3. 两端继续调用 `endpoint_step()`，完成请求确认、响应发送和接收。
4. 用 `endpoint_add_inspect()` 查看调用状态；到
   `WL_RPC_CLIENT_COMPLETED` 后，用 `quickstart_add_client_decode()` 取出结果。
5. 用 `endpoint_add_release()` 释放调用占用的位置。失败/超时终态也要回收。

`calculator_t` 是本例的业务上下文，只给处理函数传入设备端点和模拟时刻，
不包含 Wirelink 缓冲区。`request` 指针只在回调期间借用；
慢任务应复制所需参数和请求凭据，然后让通信 owner 在工作完成后提交响应。

`runtime_result_ok()` 检查一步操作是否成功；`runtime_result_rpc_detail()`
取得编号等 RPC 详情。这与“整次调用已完成”不同，后者通过 inspect 判断。
当前结果类型仍沿用 runtime 命名，但普通调用不需要创建独立 runtime。

示例从 10 到 29 手动推进模拟毫秒，不是建议的任务周期。
真实应用使用单调时钟、持续推进，并根据状态或唤醒提示安排工作。

## 6. 完整程序

[`examples/getting_started.c`](../examples/getting_started.c)：

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
                          const wl_rpc_server_request_t *token,
                          wl_delivery_t delivery) {
  calculator_t *calculator = context;
  add_response_t response;
  quickstart_runtime_result_t result;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  if (sum < INT32_MIN || sum > INT32_MAX) return -1;
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  /* Preparing a response is synchronous; endpoint_step sends it later. */
  result = quickstart_endpoint_add_complete(calculator->device, token,
                                            &response, calculator->now_ms);
  return quickstart_runtime_result_ok(&result) ? 0 : -1;
}

int main(void) {
  static quickstart_endpoint_t controller, device;
  quickstart_endpoint_config_t client_config, server_config;
  calculator_t calculator = {&device, 0U};
  wl_loopback_t cable;
  telemetry_t telemetry, received;
  add_request_t request;
  add_response_t response;
  quickstart_runtime_result_t result;
  const quickstart_runtime_rpc_detail_t *detail;
  wl_rpc_client_result_t operation;
  uint32_t operation_id;

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
  result = quickstart_endpoint_add_start(&controller, &request, 100U, 10U);
  detail = quickstart_runtime_result_rpc_detail(&result);
  CHECK(quickstart_runtime_result_ok(&result) && detail != NULL);
  operation_id = detail->operation_id;

  /* Simulated milliseconds. Real applications use their monotonic clock. */
  for (calculator.now_ms = 10U; calculator.now_ms < 30U; ++calculator.now_ms) {
    CHECK(quickstart_endpoint_step(&controller, calculator.now_ms) == WL_OK);
    CHECK(quickstart_endpoint_step(&device, calculator.now_ms) == WL_OK);
  }
  CHECK(quickstart_endpoint_add_inspect(&controller, operation_id, &operation) == WL_RPC_OK);
  CHECK(operation.state == WL_RPC_CLIENT_COMPLETED);
  result = quickstart_add_client_decode(&operation, &response);
  CHECK(quickstart_runtime_result_ok(&result) && response.sum == 42);
  CHECK(quickstart_endpoint_add_release(&controller, operation_id) == WL_RPC_OK);

  quickstart_endpoint_close(&controller);
  quickstart_endpoint_close(&device);
  puts("unreliable telemetry: sample=7 temperature=23.50 C");
  puts("reliable RPC: 20 + 22 = 42");
  return 0;
}
```

## 7. 从成功示例走向真实命令

示例中的 `ack_timeout_ms = 20` 是等待链路确认的时间，`max_retries = 2`
是最多重传两次；调用的 100 毫秒则限制应用等待整个 RPC 的时间，两者用途不同。
服务端的 1000 毫秒限制待处理请求保留时间，10000 毫秒限制完成响应的缓存寿命。
这些都是演示策略，需根据设备的实际处理时间设置。

完成响应的缓存用于处理“请求执行了，但响应丢失后又收到同一个请求”：
在缓存仍有效时，可以重发已有结果。使用同一调用编号重试时，请求内容也必须一致。
缓存容量有限，而且会过期；设备重启、会话变化也会结束这种保护。
RPC 不保证跨重启永久只执行一次。尤其是开锁、扣款等有副作用的命令，
超时只表示调用方没有及时取得结果，不能据此断言设备未执行。

生成的接收路径会观察可靠 RPC 请求携带的对端会话；会话变化时先清理旧会话的
RPC 待处理和缓存状态，再调用新请求的处理函数。
如果应用还维护权限或控制权等状态，应另外处理这次变化，
接口见[集成篇](tutorial-integration-cn.md)。

下一篇：[把示例接入自己的工程与硬件](tutorial-integration-cn.md)。
需要逐项查询客户端/服务端状态约束时，再读 [RPC 参考](rpc-runtime-cn.md)。
