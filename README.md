# Wirelink

[English](README-en.md) · [工程文档](docs/README.md) · [WLC 编译器](wlc/README.md) · [在线文档](https://docs.silkenkite.ink/wirelink/)

面向 MCU（裸机 / RTOS / Zephyr）与上位机的 **ISO C11、零堆分配**通信中间件。用一份 `.wl` 声明消息、RPC 与节点，生成一对 `.h/.c`；默认节点自带端口和定长缓冲，实现物理收发钩子即可接入 UART、USB Bulk、UDP 或 SPI。

- **核心无 OS 与时钟依赖**：应用在单一 owner 中传入时间并步进；ISR / I/O 线程只使用传输层原子边界。
- **直接填写业务结构体**：字段默认必选，不需要 `has_*`；只有 `optional` 字段带存在位。
- **固定容量与显式背压**：最多 4 个并发 RPC、3 个待发回复、8 个 TX 队列项；普通消息有一个发送存储槽。资源暂满返回 `WL_ERR_WOULD_BLOCK`，应用稍后重试。
- **Wire v2**：4 字节小端帧头、8/12 字节 RPC 子头；普通消息可选 Schema-v1 TLV 或 Positional-v1，RPC 使用 Schema-v1。

当前本地候选：**Wirelink 0.14.0 / WLC 0.14.0 / Codegen 0.14 / SDK Binding API 3**。升级需重新生成 C 与 SDK，见 [0.14 迁移说明](docs/release-v0.14.0-cn.md)。

## 三步运行 RPC

### 1. 在一个 `.wl` 中定义消息与两个角色

[`examples/01_rpc/calculator.wl`](examples/01_rpc/calculator.wl)：

<!-- [docs:calculator_schema] -->
```wl
message AddRequest @id(20) {
  int32 left @id(1);
  int32 right @id(2);
}
message AddResponse @id(21) { int32 sum @id(1); }
rpc Add(AddRequest) -> AddResponse;
node client { call Add; }
node server { serve Add; }
```
<!-- [/docs:calculator_schema] -->

节点默认 `io = embedded`、`handlers = static`、`framing = native`、`codec = schema_v1`，RPC 默认超时 500 ms。无需另写绑定文件、端口配置或 handler 注册表。

### 2. 选择节点并生成一对 C 文件

```sh
# 在仓库根目录构建本地 WLC。
cargo build --release --locked --manifest-path wlc/Cargo.toml

# 回环程序同时承载 client 和 server。
wlc/target/release/wlc compile examples/01_rpc/calculator.wl \
  --all-nodes --out-dir build/calculator

# 只部署服务端时改用 --node server；产物仍为 calculator.h/.c 和 manifest。
```

无节点时仅生成编解码器；一个节点自动选择；多个节点必须使用 `--node <name>` 或 `--all-nodes`。CMake 对应 `wirelink_wlc_generate(... NODE server)` 或 `ALL_NODES`。

### 3. 实现静态钩子，初始化并步进

服务端直接实现生成头中的函数，不需要注册：

<!-- [docs:calculator_server] -->
```c
int32_t server_on_add(server_node_t *self, const add_request_t *request, add_response_t *response) {
  (void)self;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->sum = (int32_t)sum;
  return 0;
}
```
<!-- [/docs:calculator_server] -->

在 [完整回环示例](examples/01_rpc/client.c) 中，`client_tx_write()` 将完整帧交给 `server_rx_packet()`，反向亦然。`client_init(&caller)` / `server_init(&callee)` 后即可并发发起请求：

<!-- [docs:calculator_async_rpc] -->
```c
void calculator_async_example(client_node_t *client) {
  /* Four accepted calls coexist; request values are copied during admission. */
  for (int32_t i = 0; i < 4; ++i) {
    EXAMPLE_REQUIRE(client_call_add(client, &(add_request_t){
        .left = i, .right = 42 - i}, done) == WL_OK);
  }
}
```
<!-- [/docs:calculator_async_rpc] -->

`done` 是接收 `client_node_t *`、`wl_kernel_result_t` 和响应指针的回调。请求在调用成功时已编码复制；响应仅在回调期间有效。单一 owner 持续调用 `client_step(&caller, now_ms)` / `server_step(&callee, now_ms)` 分发收发与完成事件。简化调用从**下一次 step** 开始计时；需要显式时刻、超时、上下文或取消句柄时使用 `client_call_add_ex()`。

物理钩子只能整帧同步接受或完全拒绝；忙后恢复可写时调用 `client_tx_ready()` / `server_tx_ready()`。异步 DMA 或零拷贝驱动选择 `io = external`，详见 [移植指南](docs/porting-adapters.md)。关闭前先停止 RX 生产者，再调用节点 `close()` 并步进至 `quiescent()`。

### 连接上位机

```sh
wlc/target/release/wlc sdk examples/01_rpc/calculator.wl \
  --node client --name calculator --out-dir build/calculator-sdk
```

SDK 提供 C++20 和 Python 3.10+ 的 UDP 客户端。服务端需接入 UDP 适配器；[UDP 双端示例](examples/05_zephyr_udp/) 与 [可安装 SDK 示例](examples/06_bindings/) 展示完整接法。Python 调用如下：

<!-- [docs:python_sdk_client] -->
```python
with Client.connect(Udp(peer=('127.0.0.1', port))) as client:
    saved = client.add(left=20, right=22)
    assert saved.sum == 42
    try:
        client.add(left=2**31 - 1, right=1)
    except RejectedError as error:
        assert error.status == 1
```
<!-- [/docs:python_sdk_client] -->

## 实测内存：比较完整装配，不只比较节点结构体

本机 GCC 16.2.1、`-std=c11 -Os -ffunction-sections -fdata-sections` 的对象统计：核心 12 个目标文件合计 `.text = 17,474 B`、`.rodata = 1,524 B`、`.data = 0 B`、`.bss = 4 B`。这 4 B 是进程级自动实例号分配器。实际链接由 `--gc-sections` 裁剪未用代码。

| 同一双向 demo 装配 | 0.13 基线总 RAM | 当前总 RAM | 变化 |
| --- | ---: | ---: | ---: |
| native | 782 B | 790 B | +8 B |
| COBS + CRC16（含 FIFO） | 1,046 B | 1,054 B | +8 B |
| length16 | 924 B | 932 B | +8 B |

总 RAM 包含节点、外部端口、RX 存储以及所需 stream/FIFO 对象与缓冲，不含上面的进程级全局状态。当前 `demo_node_t` 为 624 B，`wl_kernel_t` 为 440 B，`wl_packet_port_t` 为 128 B。该双向外部 I/O demo 没有 RAM 降幅；缺少相应路由的节点才能省去未使用的请求、回复或普通消息存储。复现方式与统计边界见 [footprint 指南](benchmarks/footprint/README-cn.md)。

## 30 秒本地体验（无需 WLC 或硬件）

仓库包含 [预生成 C](examples/kernel/generated/)。以下默认构建需要 C11、C++20 编译器与 CMake 3.21+；C++20 用于测试 C/C++ 端口 ABI，核心与回环示例仍为纯 C11：

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
./build/core/examples/wirelink_bare_metal_loopback
```

预期输出：

```text
6 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent
```

## 深入阅读

- [语言、CLI 与生成 API](wlc/README.md)：`optional`、节点选择、静态/动态 handler、CMake。
- [Wire v2 字节规范](docs/wire-format.md)：帧头、RPC、TLV、Positional 与流分帧。
- [移植 UART / USB / UDP / SPI](docs/porting-adapters.md)：embedded 快速接入与 external 异步租约。
- [开发与验证](docs/development.md)：主机、SDK、Zephyr、安装消费与本地发布边界。
- [教学示例](examples/) · [Zephyr 示例](samples/zephyr/) · [基准测试](benchmarks/)。

[Apache-2.0 许可证](LICENSE)
