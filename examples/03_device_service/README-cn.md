# 从一个 RPC 扩展到一组设备服务

这个示例不是性能测试，而是维护成本验收：一个设备提供 12 个 RPC，新增第 13 个时，
不应修改通信循环、分发 switch、槽位结构或回收代码。[English](README.md)。

设备是内存中的模拟工作台，不访问硬件。客户端和服务端是两个独立 C11 程序，通过 UDP 通信。
普通业务使用生成的托管 RPC；只有延迟自检使用高级完成凭据。

## 建议阅读顺序

1. 看 [device_service.c](device_service.c) 的 `get_info()`：读取业务状态、填写响应、返回 0。
   同文件的 `read_register()` 展示非零返回值如何表示业务拒绝，不需要手动回包。
2. 看 [server.c](server.c)：初始化端点，绑定业务模块，连接 UDP，然后推进。
   `config.user_data = &device` 为所有普通 handler 指定一次业务上下文。
3. 对照 [services.bind.wl](services.bind.wl) 的 `GetInfo` 与 [device.wl](device.wl) 的两条消息。
   RPC 定义只有一份，由两端共同使用；编号匹配、可靠发送、结果回收由 Wirelink 负责。
4. 看 [client.c](client.c)：先是直接获取响应的同步调用，然后是三种 RPC 的异步批次。
   异步循环只推进端点、等待完成通知，没有逐调用的 inspect/release。
5. 最后才看 [self_test.c](self_test.c)：它模拟跨三个 owner-loop pass 的工作，保存凭据后延迟回复。
   这不是真实硬件自检，也没有后台 worker。

无需先阅读生成的 runtime、存储布局或驱动内部实现。

## 业务如何组织

| 模块 | 提供的 RPC |
| --- | --- |
| `device_service.c` | Ping、GetInfo、SetName、ReadRegister、WriteRegister、GetCounters、ResetCounters |
| `calculator_service.c` | Add、Scale、GetLimits、SetLimits |
| `self_test.c` | SelfTest，唯一的延迟操作 |

每个模块的 `*_bind()` 只做类型安全的 handler 注册。这是示例的业务组织方式，不是另一个框架。
请求/响应只有业务数据，没有手动填写的 operation ID 或通用 RPC 状态字段。
生成值中的 `has_*` 表示字段已填写；本例用初始化器或赋值设置它们。

普通 handler 的上下文默认来自 `config.user_data`。某个服务若需要不同上下文，设置
`config.<service>_user_data`；NULL 表示继承公共上下文。高级 deferred handler 的上下文和
客户端完成回调的上下文仍显式指定，不会隐式继承。

## 两端为何还有各自的 profile

共享的 `services.bind.wl` 描述 RPC 合同，端侧文件只描述自己的遥测用途：

```wl
// server.bind.wl：发送，不创建接收邮箱。
profile version 1;
send DeviceTelemetry { delivery = unreliable; }
```

```wl
// client.bind.wl：接收，只保留最新值。
profile version 1;
latest DeviceTelemetry { delivery = unreliable; }
```

CMake 用 `PROFILES services.bind.wl server.bind.wl` 组合它们。WLC 不做后者覆盖前者；
重复服务、重复同方向路由以及把 RPC 消息同时声明成普通消息都会报错。
文件顺序不影响生成结果；两端 profile identity 可以不同，RPC 合同必须一致。

发送声明参与端点缓冲区上限计算，不分配 retained 存储。这里的 100 通道遥测比任意 RPC 都大，
用它验证“只发送的消息也能正确推导容量”。旧 `latest/fifo` 仍提供同 delivery 的对称发送助手；
单向发送端使用 `send` 即可，不需要虚构一个接收邮箱。

## 构建与运行

本开发迭代需要 **WLC 0.4.0 / 生成 ABI 29**。ABI 26 的公开源码快照不能生成此接口；
目前需要显式指定本轮配套的开发编译器，不会自动下载旧版代替。
WLC 可以放在任意目录，不要求嵌套在 Wirelink 仓库内。

在独立 WLC 工作区执行 `cargo build --release --locked`，确认 `wlc codegen-abi` 输出 `28`。
从 Wirelink 根目录构建：

```sh
cmake -S . -B build/device-service -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build build/device-service --config Release --parallel 2
```

在两个终端分别运行：

```sh
./build/device-service/examples/03_device_service/device_service_server
./build/device-service/examples/03_device_service/device_service_client
```

Windows 使用 `.exe`；多配置生成器的程序位于相应的 `Release/` 子目录。
默认使用本机 UDP 49300/49301。两个程序均可传入 `本地端口 对端端口`。
客户端会改变模拟设备状态，因此手动重复整套验收前请重启服务端。

## 新增第 13 个 RPC：GetBuildLabel

维护者只需要做四处业务修改：

1. `device.wl`：增加请求、响应消息及其唯一消息编号。
2. `services.bind.wl`：增加 `rpc GetBuildLabel { request = GetBuildLabelRequest; response = GetBuildLabelResponse; }`。
3. `device_service.c`：添加 handler，并在现有绑定函数中写 `config->on_get_build_label = get_build_label;`。
4. `client.c`：增加 `device_client_endpoint_get_build_label_sync(...)` 调用及业务断言。

不增加 RPC 种类 enum、不扩展中央 switch、不手算缓冲区、不新增释放分支，也不改主循环。
只提供服务、不新增演示客户端调用时，前三个文件足够。

[扩展验收脚本](../../tests/tutorials/device_service_extension.py)包含这些修改的完整代码。
它复制本示例到临时目录，实际增加服务，检查恰好这四个文件变化，重新编译并运行全部旧用例和
新 RPC；原工作区不被修改。运行：

```sh
ctest --test-dir build/device-service -C Release -R wirelink_device_service --output-on-failure
```

## 验收范围与边界

覆盖全部 12 个服务、业务拒绝、自持字符串保存、三种 RPC 的异步批次、请求接受后改写、
延迟回复以及单向大遥测。扩展测试再覆盖新增 RPC 的实际修改范围。

一个 RPC 的 timeout 包含客户端排队时间。同步调用由调用线程推进端点，不能从本端点的
handler 中递归调用。真实 worker 应把结果交回 owner，由 owner 完成延迟回复。
遥测遇到忙时允许丢弃；本例不替代产品的公平调度或最新值发送队列。
编译期服务数量不等于同时在途的调用数量；本例保留默认四槽，不做容量调优。
新服务仍须满足唯一消息编号、有界消息和单帧上限，不代表可以无限扩展。
