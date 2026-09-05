# 入门：让显示端读到最新温度

假设一个设备不断测量温度，另一个设备负责显示。显示端只关心现在的温度，
不需要把每一个历史读数都显示一遍。本篇用 Wirelink 实现这件事。

先不用开发板。我们在一个电脑程序里放一个“测温设备”和一个“显示端”，
用内存模拟它们之间的连接。Wirelink 负责把温度结构体变成可传输的数据，
并在另一端还原；程序决定什么时候发送、什么时候更新显示。

这是教程第一篇。接下来是[请求设备完成一次计算](tutorial-rpc-cn.md)，
最后是[接入自己的工程与硬件](tutorial-integration-cn.md)。
无需先读 API 总览或协议规范。[English](getting-started.md)。

## 1. 先运行，看看结果

先完成[环境准备：安装 WLC](installation-cn.md)。运行示例只需要 C11 编译器、
CMake 3.21 或更新版本，以及已经能从终端调用的匹配版 `wlc`。
Wirelink 仓库内不需要有 WLC 源码；下面从 Wirelink 根目录执行：

```sh
cmake -S . -B build/quickstart \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
cmake --build build/quickstart --target wirelink_latest_telemetry
./build/quickstart/examples/wirelink_latest_telemetry
```

预期输出：

```text
latest telemetry: sample=2 temperature=23.50 C
```

程序发送了两次测量：第 1 次 23.00 °C，第 2 次 23.50 °C。
两次都已经接收，但显示端此时才读取，所以只得到第 2 次。
下面依次看消息定义、接收策略和完整程序。

## 2. 告诉双方“温度消息长什么样”

文件 [`temperature.wl`](../examples/getting_started/temperature.wl)：

```text
version 1;

message Telemetry @id(10) {
  required uint32 sample @id(1);
  required int32 temperature_centi_c @id(2);
}
```

`Telemetry` 是我们给消息起的名字，意思是“遥测数据”，不是 Wirelink 的关键字。
它包含采样编号 `sample` 和以百分之一摄氏度为单位的温度：
`2350` 就是 23.50 °C。使用整数可以避免在消息定义中引入浮点换算问题。

`message Telemetry @id(10)` 表示温度消息的类型编号是 10，用来区分不同消息。
字段上的 `@id(1)`、`@id(2)` 则给这条消息里的两个字段编号。它们是稳定的标识，
不是测量值；后续调整字段排列时，不要改变已有编号。
`required` 表示消息必须包含这个字段；`uint32` 和 `int32` 是无符号、
有符号的 32 位整数。`version 1` 表示这是这份消息定义的第一版。

这类描述消息结构的文件称为 **schema（消息定义）**。双方用同一份定义生成代码，
就能按相同规则编码和解码；不直接把 C 结构体的内存原样发出去。

## 3. 告诉接收端“收到后怎么用”

文件 [`temperature.bind.wl`](../examples/getting_started/temperature.bind.wl)：

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}
```

这里有两个独立的选择：

| 写法 | 回答的问题 | 本例的选择 |
| --- | --- | --- |
| `latest Telemetry` | 应用还没来读时，新数据怎么存？ | 保留最新接收的温度，替换尚未读取的旧温度 |
| `delivery = unreliable` | 这类消息以什么传输方式接收？ | 不等待接收确认，也不因丢失而重传 |

`latest` 不等于 `unreliable`。前者是接收后的存储策略，后者是链路的传输方式。
温度不断刷新，偶尔丢掉一次可以等下一次，所以本例把它们组合使用。
LATEST 也可以接收可靠传输的消息；反过来，允许丢失的消息也不一定要存入 LATEST。

这里的“最新”指最后接收并交给这个存储区的值。Wirelink 不会比较 `sample`
或时间戳来替你判断哪次测量更晚。如果底层传输会乱序，筛掉旧采样是应用需要处理的事。

`.bind.wl` 中的 `bind` 是 binding 的命名约定，用来标明“把消息绑定到什么使用方式”。
它仍是文本文件，内容以 `profile version 1;` 开头，使用与 schema 不同的语法。
它不是另一份消息结构，也不是必须使用这个后缀；生成命令通过 `PROFILE` 参数选择它。
这样的文件称为 **binding profile（消息使用配置）**。

分成两个文件，是因为“发送什么数据”和“收到后怎么处理”可以独立变化：
测温设备只发送，显示端保存最新值，记录器可能想按顺序处理每次接收。
它们可以共享消息定义，使用各自的处理配置。
单纯生成编码/解码函数不需要 `.bind.wl`；本例为了自动生成 LATEST 的收取接口才用它。

本例使用端点的 `send_telemetry()`，它直接采用这里声明的 `unreliable`，
不用在每次发送时重复选择。高级的 codec 发送接口仍允许显式指定传输方式。

## 4. 代码里只需要三个通信对象

`temperature_endpoint_t` 是 WLC 根据消息定义和使用配置生成的端点类型，
已经包含 Wirelink 连接状态、消息处理代码以及需要的静态存储。
你不需要自己定义这个结构体，也不需要给内部缓冲区挑选字节数。

`device` 和 `display` 是这个类型的两个实例，分别代表测温设备和显示端。
它们不是网络地址。`static` 让对象从零初始化并在程序运行期间保持有效；
已初始化的端点不能复制或移动。

`cable` 是模拟连接，负责把两个端点连起来。硬件上会换成串口等适配器。
`endpoint_handle()` 提供适配器连接所需的通用端点入口：
它让同一个适配器也能连接由不同消息配置生成的端点。

初始化中的 1、2 是本次模拟运行的非零会话标识。先把它们视为这个隔离实验的常量；
可靠通信需要怎样处理重启，在第二篇解释。

## 5. 完整 C 代码

下面就是 [`examples/latest_telemetry.c`](../examples/latest_telemetry.c)，
没有隐藏初始化函数。`CHECK` 是本示例的错误检查宏：结果不符合预期就打印行号并退出，
不是 Wirelink API。`temperature_runtime.h` 是生成的头文件，其中也声明了默认端点接口。

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>

#include "temperature_runtime.h"
#include "wirelink/loopback.h"

/* Desktop example: print the failing expression and stop on unexpected errors. */
#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
    return 1; \
  } \
} while (0)

int main(void) {
  static temperature_endpoint_t device, display;
  wl_loopback_t cable;
  telemetry_t received;

  /* Fixed IDs are only for this isolated simulation. */
  CHECK(temperature_endpoint_init(&device, 1U) == WL_OK);
  CHECK(temperature_endpoint_init(&display, 2U) == WL_OK);
  CHECK(wl_loopback_connect(&cable, temperature_endpoint_handle(&device),
                           temperature_endpoint_handle(&display)) == WL_OK);

  for (uint32_t sample = 1U; sample <= 2U; ++sample) {
    telemetry_t message;
    telemetry_clear(&message);
    message.has_sample = true;
    message.sample = sample;
    message.has_temperature_centi_c = true;
    message.temperature_centi_c = sample == 1U ? 2300 : 2350;

    CHECK(temperature_endpoint_send_telemetry(&device, &message).domain
          == TEMPERATURE_SEND_OK);
    /* Each endpoint advances transport and message handling in one call. */
    CHECK(temperature_endpoint_step(&device, sample) == WL_OK);
    CHECK(temperature_endpoint_step(&display, sample) == WL_OK);
  }

  CHECK(temperature_endpoint_read_telemetry(&display, &received) == WL_OK);
  CHECK(received.sample == 2U && received.temperature_centi_c == 2350);
  printf("latest telemetry: sample=%u temperature=%.2f C\n",
         (unsigned)received.sample, received.temperature_centi_c / 100.0);
  CHECK(temperature_endpoint_read_telemetry(&display, &received) == WL_ERR_NO_DATA);

  temperature_endpoint_close(&device);
  temperature_endpoint_close(&display);
  return 0;
}
```

## 6. 按一次温度更新理解调用过程

`telemetry_clear()` 清空消息，再设置两个值和对应的 `has_...` 标志。
标志表示“这个字段已填写”；数值零也是有效数据，不能代表缺失。

`temperature_endpoint_send_telemetry()` 编码并提交消息，传输方式来自 `.bind.wl`。
`TEMPERATURE_SEND_OK` 只表示本地接受了发送，不证明显示端已经收到。

Wirelink 不创建后台线程。对两个端点调用 `endpoint_step()`，
才会推进模拟传输、处理收到的消息、回收发送完成状态。
这里每次传入的 `sample` 也用作手动推进的模拟毫秒时刻；
真实程序传入单调毫秒时钟，并持续在一个通信线程或主循环中运行它。

显示端的 `endpoint_read_telemetry()` 把最新值复制到你的 `received` 变量中。
这份数据归你，可以继续保存或修改；不用手动归还借用指针。
没有新数据时返回 `WL_ERR_NO_DATA`，并保持输出变量不变。它不是通信故障。

默认的一轮推进有工作量上限，不保证一次调用处理完所有排队的消息。
本例每次只发一条，所以依次推进两端就足够。关闭时对两个端点调用
`endpoint_close()`，让已连接的适配器停止访问存储；共享的 `cable` 要活到两端都关闭。
示例失败时直接结束电脑进程，长期运行的应用应在错误退出路径也关闭端点。

## 下一步

把第二次温度改成 `2410`，重新构建，应显示 24.10 °C。
再把读取过程移到循环内、每次推进显示端之后，就能读到两次测量。

如果业务改成“让设备执行命令，并告诉我结果”，继续读
[第二篇：请求设备完成一次计算](tutorial-rpc-cn.md)。
想调整缓冲区、接入 DMA 或定制调度时，再读[集成篇](tutorial-integration-cn.md)；
这些不是运行本例的前置知识。
