# 第三篇：接入自己的工程与硬件

现在你已经能[接收最新温度](getting-started-cn.md)，并能[请求一次计算](tutorial-rpc-cn.md)。
本篇解释怎样把这两种用法搬到自己的项目，再替换内存连接。
先完成电脑上的独立工程，就能把构建问题与硬件问题分开排查。
[English](tutorial-integration.md)。

## 1. 生成代码为什么分成 codec 和 runtime

普通应用使用前两篇的默认端点即可。这里解释端点内部的分工，方便决定构建依赖，
不是要求你重新手动组装这些对象。

回忆温度例子，程序做了两类事：

- 把 `telemetry_t` 转成字节，再把字节转回结构体。这部分叫 **codec（编码器/解码器）**。
- 收到温度后保存最新值，或为 RPC 记录尚未完成的请求。这部分叫 **runtime（运行时辅助代码）**。

只发送温度的设备需要编码和发送函数，不需要接收方的 LATEST 存储。
而同一份消息定义可以供显示端和记录器使用，它们的接收策略不同。
所以我们允许多个 runtime 共享一份 codec，避免每种使用方式都重复生成同名编解码函数。

**target（构建目标）** 是 CMake 给一组可编译文件或一个程序起的名字，
不是“目标开发板”。`temperature_codec` 就是一个由生成的 C 文件构成的库目标；
`temperature_protocol` 则包含 profile 指定的接收辅助代码，并依赖前一个目标。
目标名可自选。生成函数中的 `temperature_` 前缀默认来自 schema 文件名，
不是来自 CMake target 名。

## 2. 建一个独立的温度显示工程

新建一个目录，放入这四个文件：

| 文件 | 内容 |
| --- | --- |
| `main.c` | 完整复制上一篇的 [`latest_telemetry.c`](../examples/latest_telemetry.c) |
| `temperature.wl` | [消息定义](../examples/getting_started/temperature.wl) |
| `temperature.bind.wl` | [LATEST 使用配置](../examples/getting_started/temperature.bind.wl) |
| `CMakeLists.txt` | 下面的完整构建配置 |

```cmake
cmake_minimum_required(VERSION 3.21)
project(temperature_display LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Wirelink CONFIG REQUIRED)
wirelink_wlc_generate_codec(
  TARGET temperature_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/temperature.wl")
wirelink_wlc_generate_runtime(
  TARGET temperature_protocol
  CODEC_TARGET temperature_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/temperature.bind.wl")
add_executable(temperature_display main.c)
target_link_libraries(temperature_display PRIVATE
  temperature_protocol Wirelink::loopback)
```

`wirelink_wlc_generate_codec()` 读取 schema，调用 WLC，编译编码、解码和类型化发送代码。
`wirelink_wlc_generate_runtime()` 读取 binding profile，生成默认端点、温度接收与 LATEST 访问代码。
`CODEC_TARGET` 指出它使用哪份消息代码；`PROFILE` 指出它使用哪份消息处理配置。
链接 `temperature_protocol` 会带入所需 codec 和 Wirelink 核心；
`Wirelink::loopback` 为这个演示额外提供内存连接。

在 Wirelink 根目录，先构建全部库（第一篇只构建了遥测目标），再安装到本地目录：

```sh
cmake --build build/quickstart --parallel
cmake --install build/quickstart --prefix "$PWD/build/tutorial-install"
```

然后构建自己的工程。将下面的 `/path/to` 和 `/absolute/path/to` 替换成你的真实路径：

```sh
cmake -S /path/to/temperature-display -B /path/to/temperature-display/build \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/wirelink/build/tutorial-install \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
cmake --build /path/to/temperature-display/build
/path/to/temperature-display/build/temperature_display
```

输出应仍然是 `latest telemetry: sample=2 temperature=23.50 C`。
这里 WLC 只在构建时运行，部署的是可执行程序或编译进固件的 C 代码。
本开发分支需要生成 ABI 19 的 WLC，按[安装篇](installation-cn.md)独立安装并加入 PATH。

只使用类型化发送、自己处理接收的工程可以只链接 codec。
需要接收辅助功能时再链接相应 runtime；更多角色拆分和命名选项见 [WLC 指南](https://github.com/starwey604/wlc/blob/31df0e0dae644f380b57e9b2d69a96aa56be0f58/README-cn.md)。

## 3. 不要把两种“配置”混为一谈

`.bind.wl` 的 **binding profile** 选择消息如何进入 LATEST、FIFO 或 RPC。
**FIFO** 是先收到先取出的有界队列，适合想逐条处理的记录；队满时也需要应用处理失败，
不是“无限保存且绝不丢失”。

`wl_config_t` 则配置连接如何传包。旧文档有时把这组参数也称为 link profile。
它不是 `.bind.wl` 文件，字段用途如下：

| 字段或术语 | 用通俗语言理解 | 怎么选择 |
| --- | --- | --- |
| `envelope`，封装方式 | 接收方怎么知道一个包从哪里开始、到哪里结束 | 根据实际传输是否保留包边界选择 |
| `integrity`，完整性校验 | 如何检测传输中的意外字节损坏 | 两端约定相同模式，例如 CRC32C |
| `max_payload_len`，payload bound | 编码后一条消息内容最多多少字节 | 覆盖要发送/接收的消息，不含包头和校验 |
| `max_transmission_unit` | 底层允许的完整包有多大 | 覆盖消息、包头、校验和封装开销 |
| `session_id` | 区分这次启动与旧启动的可靠流量 | 按[第二篇](tutorial-rpc-cn.md)设计非零启动标识 |
| `ack_timeout_ms`、`max_retries` | 可靠发送等多久重试、最多重试几次 | 考虑传输延迟、对端调度和故障恢复要求 |

“带外配置”就是你在固件/主机程序里预先约定这些值；Wirelink v1 不会自动交换和协商它们。
入门时两端采用同样的封装、校验和容量最简单。
从协议角度，容量是本地限制，并非全部必须数值相等；
关键是任一方向发出的完整包都不能超过对端与底层传输能接收的容量。

默认端点根据 profile 中的消息推导 payload 上限，并为各封装的最大开销、
一包的串口接收字节和消息 runtime 预留存储。配置以 `endpoint_config_defaults()` 开始，
改 `config.link.envelope` 等字段，再调用 `endpoint_init_config()`。
RPC 角色、超时和 handler 仍通过 `config.runtime` 选择，不会自动替产品决定重试策略。

`*_HAS_DEFAULT_ENDPOINT` 为 1 时有完整默认类型；被 profile 选中的消息无界或超出
单帧 2048 字节能力时为 0。未被 profile 选中的大消息不会放大端点。
想处理其他消息时，应调整 profile，或选择高级的 codec/自定义接收路径。

DMA 特定内存区域、更深接收队列、更多并发槽等需求仍可使用高级组装：
用 `wl_config_requirements()` 和 `runtime_requirements()` 得到容量，提供外部存储，
再用 `wl_endpoint_init()` 组合核心和应用 hooks。默认端点不支持运行中自动扩容。

CRC 检测损坏，不认证发送者，也不加密内容。
需要防窃听或验证对方身份时，应在产品的传输/安全层解决。

## 4. 把内存连接换成真实传输

**adapter（适配器）** 是 Wirelink 和驱动之间的连接代码：
把待发的字节交给驱动，把接收的字节交给 Wirelink。

| 实际接入方式 | 封装选择 | 为什么 |
| --- | --- | --- |
| UART 或 USB CDC 串口字节流 | `WL_ENVELOPE_COBS_STREAM` | 串口只有连续字节；COBS 编码和分隔符帮助恢复包边界 |
| 每次交付完整数据包的接口，例如 UDP datagram | `WL_ENVELOPE_NATIVE_PACKET` | 接口已经给出包边界 |
| 使用 Wirelink 支持的 16 位长度前缀格式的总线接口 | `WL_ENVELOPE_BUS_LENGTH16` | 根据长度字段划分包 |

不能仅凭“USB”或“CAN”这个名字选择封装。
例如 USB CDC 是字节流；其他 USB 或 CAN 适配器是否交付完整 Wirelink 包，
取决于它们是否提供必要的分包/组包处理。
先按[适配器文档](adapters-cn.md)选择已有实现，再检查其输入约定。

自己写适配器时，`wl_set_sink()` 注册发送入口。
返回 `WL_SINK_SENT` 表示同步用完字节；`WL_SINK_STARTED` 表示驱动还在借用它们，
稍后由通信处理线程调用一次 `wl_tx_complete()` 通知完成。
`WL_SINK_BUSY` 表示暂时不能发送，`WL_SINK_FAILED` 表示本次 I/O 失败。

接收整包用 `wl_feed_unit()`，接收字节流可用 `wl_feed_bytes()`；
DMA 等减少复制的接口属于后续优化，先读懂基本路径再选择。

## 5. 谁来持续推进通信

默认路径只调用 `*_endpoint_step()`：已附加适配器的 service、事件清理和 runtime
进度都包含在内。`*_endpoint_handle()` 给出通用端点入口；休眠提示用
`wl_endpoint_get_hint()` 查询。常规 LATEST/FIFO 的 `endpoint_read_*()` 返回用户拥有的副本。
大消息希望免复制时，可通过 `endpoint_runtime()` 使用原有 acquire/release 接口。

接入已有硬件适配器时，用 `wl_endpoint_link(endpoint_handle(...))` 获得它需要的
核心指针，再通过 `wl_endpoint_attach()` 接入该适配器的 service/quiesce/deadline hooks。
目前 loopback 的 `wl_loopback_connect()` 已自动完成这两步；其他平台仍需集成层连接。
以下是这层集成必须遵守的执行规则，普通业务代码无需自己重建 hooks。

为每条连接指定一个通信处理线程或裸机主循环，文档称它为 **owner**。
发送、pump、RPC 操作和发送完成通知都在这个执行上下文进行，避免同时修改连接状态。
中断/驱动回调可发布接收数据，并记录发送完成信息、唤醒 owner；
不要在中断里执行 RPC 处理函数。

实际运行的每轮工作是：

1. 从自己的单调时钟取一次毫秒时间。
2. 处理适配器的收发完成，再运行 `wl_pump_step()`。
   可把适配器 service 回调接入 pump，也可像示例一样先显式调用。
3. 查看收到的最新值或 RPC 状态，执行有界的应用工作。
4. 睡眠前用 `wl_pump_get_hint()` 查询是否还有立即工作以及多久后需要处理超时；
   同时允许接收、发送完成、可写状态等外部事件唤醒它。

生成的 `runtime_pump_hooks()` 会接入消息分发、接收事件释放、对应 RPC 发送结果回收、
待发响应和 RPC 超时推进。它不是后台线程，创建它之后仍须持续调用 pump。
处理函数也不应长时间阻塞：较慢的计算可以交给应用任务，再让 owner 提交结果。

同一个端点已经由 generated pump 处理的接收事件不要再手动 release，
对应的发送终态也不要重复 take。高级手动调度与借用规则见 [API 边界](api-boundary-cn.md)。

关闭时先停止外部生产者，让适配器停止或结束正在借用的收发操作（称为 **quiesce**），
确认不再访问存储，再销毁或重新初始化端点。已初始化的连接、runtime 与存储不能随意复制或搬家。

## 6. 调试与重启处理

初始化失败时，生成的 `runtime_init_checked()` 比普通 `runtime_init()` 多接收一个
诊断输出参数。查看 `issue`、`field`、`required`、`provided`，
可以知道哪个配置不合适、所需和实际容量分别是多少。
配置验证后可以使用较精简的普通初始化入口。

通信中先检查函数返回值、`wl_pump_result_t` 的 `poll_errors` / `service_errors`，
以及生成 runtime 的结果回调；需要更多现场信息时，链接可选的
`Wirelink::diagnostics`，把计数器格式化到自己的日志，见[诊断参考](diagnostics.md)。

默认端点的一轮错误从 `endpoint_step()` 返回；runtime 详情由 `endpoint_result()` 获取，
底层推进详情由 `wl_endpoint_last_step(endpoint_handle(...))` 获取。
同一轮后续成功事件不会覆盖首个 runtime 错误。可靠发送终态由端点回收，
不要再手动 take；如果需要自己管理传输句柄，应选择高级手动调度路径。

RPC 对端会话变化时，结果中的 `rpc->peer_changed` 表示有变化，
`runtime_peer_observation_take()` 可取出前后两个会话编号供应用更新状态。
生成 runtime 负责 RPC 状态清理；撤销旧控制权等业务动作仍由产品完成。
如果一条可靠的非 RPC 消息也代表新对端会话，应在应用它之前显式调用
`runtime_peer_observe()`。函数名前加你实际生成的模块前缀，例如 `quickstart_`。

## 接下来按需求查阅

至此已经完成“收到最新值 → 请求执行并取结果 → 接入工程和驱动”。
无需按顺序读完所有参考文件：

- 想审阅公开 API 的划分和所有权：读 [API 边界](api-boundary-cn.md)。
- 想设计更多消息：读 [schema](schema-v1-cn.md) 与 [WLC](https://github.com/starwey604/wlc/blob/31df0e0dae644f380b57e9b2d69a96aa56be0f58/README-cn.md)。
- 想了解保留最新值或队列的限制：读 [LATEST](latest-mailbox-cn.md) 与 [FIFO](fifo-cn.md)。
- 想处理 RPC 失败/重试：读 [RPC runtime](rpc-runtime-cn.md)。
- 想传大对象：读[应用层参考](application-layer-cn.md)中的 Bulk。
- 想修改协议或规划升级：读[协议](protocol-cn.md)与[兼容性](compatibility-cn.md)。
