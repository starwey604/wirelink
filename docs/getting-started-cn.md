# 入门：让显示端读到最新温度

测温设备不断产生新读数，显示端只关心当前温度，不需要逐个回放历史读数。
本篇用两个独立程序实现：`telemetry_publisher` 发布温度，`telemetry_subscriber` 显示最新值。
先不用开发板，它们通过本机 UDP 通信；UDP 是按包发送数据的传输方式，不保证每包到达。

这是第一组示例 [`00_telemetry`](../examples/00_telemetry/)。下一篇是
[请求设备计算加法](tutorial-rpc-cn.md)，再之后是[工程集成](tutorial-integration-cn.md)。
[English](getting-started.md)。

## 1. 先运行两个程序

先完成[环境准备](installation-cn.md)，安装匹配的 WLC 并获取 standalone Asio。
从 Wirelink 根目录执行，把 Asio 路径换成你的实际路径：

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DWIRELINK_BUILD_GETTING_STARTED=ON -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio-source/asio/include
cmake --build build/tutorials --config Release --parallel
```

先在终端 A 启动接收程序：

```sh
./build/tutorials/examples/00_telemetry/telemetry_subscriber
```

看到 `telemetry subscriber ready` 后，十秒内在终端 B 启动发送程序：

```sh
./build/tutorials/examples/00_telemetry/telemetry_publisher
```

发布端每 200 毫秒发一次，共五次。接收端应能看到类似：

```text
latest sample=1 temperature=23.50 C
latest sample=2 temperature=23.50 C
latest sample=3 temperature=23.50 C
latest sample=4 temperature=23.50 C
latest sample=5 temperature=23.50 C
```

不保证每个采样都显示；接收端收到第 5 次后退出，十秒还没收到则报告失败。
Windows 的 Visual Studio 多配置构建，将程序路径改成
`build/tutorials/examples/00_telemetry/Release/telemetry_subscriber.exe`，
publisher 同理。不需要虚拟串口或 Python 转发。

## 2. 消息定义：发送什么数据

完整 [`telemetry.wl`](../examples/00_telemetry/telemetry.wl)：

```text
version 1;

message Telemetry @id(10) {
  required uint32 sample @id(1);
  required int32 temperature_centi_c @id(2);
}
```

`Telemetry` 是本例的消息名称，不是关键字。`sample` 是采样编号，温度用百分之一摄氏度表示：
2350 就是 23.50 °C。`uint32`、`int32` 分别是无符号、有符号 32 位整数。
`required` 表示必须填写。`@id(10)` 区分消息类型，字段的 `@id(1)`、`@id(2)`
标识消息里的数据，不是默认值。已有编号不要随字段排列改变。`version 1` 是这份定义的第一版。

这种消息定义称为 **schema**。双方用同一份定义生成编解码代码，就能理解对方发来的字节，
而不是直接传输 C 结构体的内存。

## 3. 消息使用配置：收到以后怎样处理

完整 [`telemetry.bind.wl`](../examples/00_telemetry/telemetry.bind.wl)：

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}
```

这里是两个独立选择：

- `latest Telemetry`：保留最后接收的值，新值覆盖应用尚未读取的旧值。
- `delivery = unreliable`：不等待链路确认，也不因丢失而重传。

**latest 不等于 unreliable。** 前者选择接收后的存储方式，后者选择传输方式。
不断刷新的温度可以等下一次更新，所以本例把两者组合使用。
“最新”是最新接收的值；库不会比较 `sample` 来筛掉乱序到达的旧测量。

`.bind.wl` 是独立的 **binding profile（消息使用配置）**，不是另一份消息结构。
这个后缀只是命名约定，构建中的 `PROFILE` 参数选择具体文件。
分开后，同一份消息定义可以供显示端保存最新值，也可以供其他程序按顺序记录。
默认端点的发送接口采用这里的传输策略，不必每次发送时重复选择。

## 4. 先认识代码里两类接口

`telemetry_endpoint_t` 是 WLC 生成的端点类型，包含通信状态和有界静态存储，
不用自己定义结构体或填写内部缓冲区。每个程序只拥有自己的一个端点。
它从 `telemetry.wl` 得到业务名称；目录编号 `00` 不进入 API。

`telemetry_endpoint.h` 是生成头文件。`tutorial_host.h` 则是
[公开的示例支持代码](../examples/common/tutorial_host.h)，不是生成文件或核心 API：

- `example_udp_open()` 在本机打开 UDP，并把适配器接到已初始化的端点。
- `example_clock()` 提供取时间的函数，初始化时交给端点，以后由 Wirelink 内部调用。
  `example_now_ms()` 只用于示例自己的发布间隔和退出时间；发包、推进不再传时间。
  `example_session_id()` 为本次运行生成非零随机标识。
- `example_udp_wait()` 等待数据或最近截止时间，不处理业务，也不创建后台通信线程。
- `example_udp_close()` 关闭端点并释放主机适配器。`CHECK` 只是打印错误并结束示例的宏。

这些函数把 Windows/Linux/macOS 的平台接入集中在
[一个 C++ 文件](../examples/common/tutorial_host.cpp)，业务程序仍是 C11。
其内部使用库自带的 Asio UDP 适配器；不是用 Python 模拟 Wirelink。

## 5. 发布端完整代码

[`publisher.c`](../examples/00_telemetry/publisher.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static telemetry_endpoint_t publisher;
  uint16_t local = 49000, peer = 49001;
  telemetry_t value;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(telemetry_endpoint_init(&publisher, example_session_id(), example_clock()) == WL_OK);
  example_udp_t *udp = example_udp_open(telemetry_endpoint_handle(&publisher), local, peer);
  CHECK(udp != NULL);

  telemetry_clear(&value);
  value.has_sample = true;
  value.has_temperature_centi_c = true;
  value.temperature_centi_c = 2350;
  for (value.sample = 1; value.sample <= 5 && example_running(); ++value.sample) {
    CHECK(telemetry_endpoint_send_telemetry(&publisher, &value).domain == TELEMETRY_SEND_OK);
    printf("published sample=%u temperature=23.50 C\n", (unsigned)value.sample);
    /* Publish every 200 ms; keep servicing the endpoint while waiting. */
    const wl_time_ms_t started = example_now_ms();
    while ((wl_time_ms_t)(example_now_ms() - started) < 200U && example_running()) {
      CHECK(telemetry_endpoint_step(&publisher) == WL_OK);
      const uint32_t elapsed = example_now_ms() - started;
      if (elapsed < 200U) CHECK(example_udp_wait(udp, 200U - elapsed) == WL_OK);
    }
  }
  example_udp_close(udp);
  return 0;
}
```

`telemetry_clear()` 清空消息，`has_...` 表示字段已填写；数值零也可以是有效值。
`send_telemetry()` 成功只表示本地提交，不证明对端收到。
间隔内持续调用 `endpoint_step()` 推进收发，再用等待函数休眠。

## 6. 接收端完整代码

[`subscriber.c`](../examples/00_telemetry/subscriber.c)：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static telemetry_endpoint_t subscriber;
  uint16_t local = 49001, peer = 49000;
  telemetry_t value;
  int complete = 0;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(telemetry_endpoint_init(&subscriber, example_session_id(), example_clock()) == WL_OK);
  example_udp_t *udp = example_udp_open(telemetry_endpoint_handle(&subscriber), local, peer);
  CHECK(udp != NULL);
  puts("telemetry subscriber ready");
  fflush(stdout);

  const wl_time_ms_t started = example_now_ms();
  while (example_running() && (wl_time_ms_t)(example_now_ms() - started) < 10000U) {
    CHECK(telemetry_endpoint_step(&subscriber) == WL_OK);
    const int result = telemetry_endpoint_read_telemetry(&subscriber, &value);
    if (result == WL_OK) {
      printf("latest sample=%u temperature=%.2f C\n", (unsigned)value.sample,
             value.temperature_centi_c / 100.0);
      if (value.sample >= 5) { complete = 1; break; }
    } else {
      CHECK(result == WL_ERR_NO_DATA);
    }
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  example_udp_close(udp);
  if (!complete) fputs("no final sample received (UDP telemetry may be lost)\n", stderr);
  return complete ? 0 : 1;
}
```

`endpoint_step()` 接收并处理消息，`read_telemetry()` 将最新值复制到自己的变量。
没有新值时返回 `WL_ERR_NO_DATA`，并非通信失败；读出的副本不用归还。
一轮推进有工作量上限，不是“无限处理直到队列为空”。

两个程序默认使用 49000、49001 端口并互相指定对端，只接受来自约定端口的包。
端口被占用时，可以分别追加 `49010 49011` 和 `49011 49010`。
UDP 端口是操作系统地址，不是 Wirelink session ID；后者的重启约束放在集成篇。

## 下一步

把 publisher 的温度改成 2410，再构建，应显示 24.10 °C。
默认例子不是可靠投递或性能基准。旧单进程例子保留在
[`tests/tutorials/loopback/`](../tests/tutorials/loopback/) 做确定性回归。

如果需要“执行命令并返回结果”，继续读[第二篇：RPC 加法](tutorial-rpc-cn.md)。
微调存储或换成真实串口时，再读[集成篇](tutorial-integration-cn.md)。
