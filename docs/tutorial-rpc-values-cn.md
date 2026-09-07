# 第三篇：查询并保存设备信息

加法响应只有一个数字。这次设备返回名称和固件版本：你可能想把它们保存到界面，
而不想关心通信缓冲区何时被复用。Wirelink 的普通响应是**自持值**：
生成的结构体内就有字符串数组，赋值会连同字符一起复制，不需要分配器或释放函数。
[English](tutorial-rpc-values.md)。

## 1. 运行两个程序

先完成[环境准备](installation-cn.md)和[加法 RPC](tutorial-rpc-cn.md)的构建。
在两个终端分别运行：

```sh
./build/tutorials/examples/02_device_info/device_info_server
./build/tutorials/examples/02_device_info/device_info_client
```

这里的两行分别在服务端、客户端终端执行，不是在同一个终端依次执行。
Windows 多配置构建使用 `02_device_info/Release/*.exe`。
首次客户端输出 `saved name=demo-sensor firmware=dev query=1`。
它实际查询两次；再次运行会显示 query=3，因为保存的是这次进程的第一份结果。
服务端按 Ctrl+C 退出，默认端口为 49201/49200。

## 2. 约定字符串上限

完整 [device_info.wl](../examples/02_device_info/device_info.wl)：

```text
version 1;

message InfoRequest @id(30) {}

message InfoResponse @id(31) {
  required string<31> name @id(1);
  required string<15> firmware @id(2);
  required uint32 query_count @id(3);
}
```

`string<31>` 表示最多 31 个 UTF-8 **字节**，不是 31 个汉字。
生成的 `name.length` 记录字节数，`name.data[]` 保存内容；`has_name` 表示字段存在。
这里空的请求表示“查询本设备”，不需要人为增加无用参数。
`query_count` 是演示用业务计数，不是 Wirelink 的调用编号。

完整 [device_info.bind.wl](../examples/02_device_info/device_info.bind.wl)：

```text
profile version 1;

rpc GetInfo {
  request = InfoRequest;
  response = InfoResponse;
}
```

这仍是普通可靠 RPC，不需要为字符串增加新的通信模型。

## 3. 服务端：填写拥有存储的响应

完整 [server.c](../examples/02_device_info/server.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "device_info_endpoint.h"
#include "tutorial_host.h"

static int32_t get_info(void *context, const info_request_value_t *request,
                        info_response_value_t *response) {
  uint32_t *queries = context;
  const char name[] = "demo-sensor";
  const char firmware[] = "dev";
  (void)request;
  response->has_name = response->has_firmware = response->has_query_count = true;
  response->name.length = sizeof(name) - 1U;
  memcpy(response->name.data, name, sizeof(name) - 1U);
  response->firmware.length = sizeof(firmware) - 1U;
  memcpy(response->firmware.data, firmware, sizeof(firmware) - 1U);
  response->query_count = ++*queries;
  return 0;
}

int main(int argc, char **argv) {
  static device_info_endpoint_t server;
  device_info_endpoint_config_t config;
  uint32_t queries = 0;
  uint16_t local = 49201, peer = 49200;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(device_info_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);

  config.on_get_info = get_info;
  config.get_info_user_data = &queries;
  CHECK(device_info_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(device_info_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("device info server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(device_info_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(device_info_endpoint_close(&server) == WL_OK);
  example_udp_close(udp);
  return 0;
}
```

框架先清理响应，再调用 handler。我们设置 presence、长度并复制实际字节。
这里的固定文本小于 schema 上限；从外部输入填充时必须先检查长度，
不能用 `memcpy` 越过生成数组。局部字符串只用于本次复制，不会被框架长期借用。
`get_info_user_data` 是业务上下文，本例指向服务端查询计数，不是协议存储。

## 4. 客户端：保存结果再关闭通信

完整 [client.c](../examples/02_device_info/client.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static device_info_endpoint_t client;
  info_request_value_t request;
  info_response_value_t response;
  uint16_t local = 49200, peer = 49201;
  CHECK(example_ports(argc, argv, &local, &peer));
  info_request_value_clear(&request);
  CHECK(device_info_endpoint_init(&client, wl_platform_environment()) == WL_OK);
  example_udp_t *udp = example_udp_open(device_info_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);

  wl_rpc_completion_t result = device_info_endpoint_get_info_sync(&client, &request, &response, 1500U);
  if (result.status != WL_RPC_SUCCESS) {
    fprintf(stderr, "GetInfo failed: %s\n", wl_rpc_status_str(result.status));
    CHECK(device_info_endpoint_close(&client) == WL_OK);
    example_udp_close(udp);
    return 1;
  }
  const info_response_value_t saved = response; /* Copies the strings too. */
  result = device_info_endpoint_get_info_sync(&client, &request, &response, 1500U);
  info_response_value_clear(&response); /* Does not change saved. */
  CHECK(device_info_endpoint_close(&client) == WL_OK);
  example_udp_close(udp);
  CHECK(result.status == WL_RPC_SUCCESS);

  /* These fields remain usable after another call and endpoint close. */
  printf("saved name=%.*s firmware=%.*s query=%lu\n",
         (int)saved.name.length, saved.name.data,
         (int)saved.firmware.length, saved.firmware.data,
         (unsigned long)saved.query_count);
  return 0;
}
```

关键是 `saved = response`。之后再次调用、清理原 response、关闭端点和 UDP，
都不会修改 saved 中的字符串。保存结果的生命周期是这个普通 C 变量的生命周期，
不是“任何指针永远有效”。回调收到的响应指针仍只在回调内有效，需要保存时复制值。

打印使用长度限制而不是假定所有字符串适合 `strlen`。协议允许嵌入 NUL；
`%.*s` 遇到 NUL 仍会停止，完整二进制日志应按长度输出。不要把字符串视作任意 bytes。

## 5. RAM 的代价在哪里

有界数组计入值类型和端点的静态容量，换来稳定的所有权；不是零复制，也不是无限长度。
本例没有堆分配。大数据或需要精确控制借用时再读[高级 RPC](rpc-runtime-cn.md)
和 [Bulk](bulk-performance-cn.md)，可选分配器只改变端点存储来源。

接着可以阅读[异步客户端](tutorial-rpc-async-cn.md)、
[延迟服务](tutorial-rpc-deferred-cn.md)，或直接[接入自己的项目](tutorial-integration-cn.md)。

