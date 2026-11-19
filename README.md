# Wirelink

[English](README-en.md) · [工程文档](docs/README.md) · [WLC 编译器手册](wlc/README.md) · [在线交互式文档](https://docs.silkenkite.ink/wirelink/)

面向嵌入式 MCU（裸机 / RTOS / Zephyr）与上位机的 **纯 ISO C11、零堆分配（0 `malloc`）** 通信中间件。

只需写一份 `.wl` 协议文件定义消息（`message`）、远程调用（`rpc`）与通信两端角色（`node`），即可自动生成一对干净的 `.h/.c` 下位机固件节点代码，以及带完整类型提示的 **Python / C++20 上位机客户端 SDK**。

- **纯粹的单线程 `step(now_ms)` 驱动模型**：C11 核心不依赖任何操作系统、线程、锁或系统时钟 API；主循环传入当前毫秒时间戳并步进，中断（ISR）与 DMA 仅操作无锁传输边界。
- **双向 RPC 与单向 Telemetry 协同**：同一份协议同时支持一问一答的双向 RPC（`call` / `serve`，自动管理会话、超时、取消与业务拒绝码）与高频单向遥测/事件推送（`send` / `recv`）。
- **按角色精确裁剪的固定静态内存**：默认节点内嵌端口与定长收发缓冲（最多 4 路并发出站 RPC、3 个待发回复、1 个普通消息快照槽、8 项 TX 调度队列）。未声明的角色分配 **0 字节**；槽位暂满时显式返回 `WL_ERR_WOULD_BLOCK`。
- **正交的线缆分帧**：一行 `framing = cobs_crc16;` 即可在 UART 串口字节流上自动完成 `0x00` 断包与 CRC-16/CCITT-FALSE 校验；UDP 等自带包边界通道使用默认的 `framing = native;`。

---

## 三步打通你的第一个上下位机通信

### 第一步：在一份 `.wl` 中定义消息、RPC 与两端角色

查看 [`examples/01_rpc/calculator.wl`](examples/01_rpc/calculator.wl)：

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

节点默认采用 `io = embedded; handlers = static; framing = native; codec = schema_v1; timeout_ms = 500;`，自带内部收发缓冲区并直接链接同名静态 C 函数，无需编写初始化配置表。

### 第二步：用 `wlc` 生成下位机 C11 节点与上位机 SDK

```sh
# 1. 在仓库根目录构建 WLC 编译器
cargo build --release --locked --manifest-path wlc/Cargo.toml

# 2. 为下位机 MCU 固件生成服务端 C11 代码（--node server）
wlc/target/release/wlc compile examples/01_rpc/calculator.wl \
  --node server --out-dir build/server

# 3. 为上位机生成 Python / C++20 客户端 SDK（--node client）
wlc/target/release/wlc sdk examples/01_rpc/calculator.wl \
  --node client --name calculator --out-dir build/calculator-sdk
```

*(提示：若想在同一个 C 进程内同时运行 `client` 和 `server` 做本地回环测试，将 `--node server` 替换为 `--all-nodes` 即可；在 CMake 工程中可直接使用 `wirelink_wlc_generate(... NODE server)` 或 `ALL_NODES` 自动管理生成与依赖追踪。)*

### 第三步：在下位机实现业务函数，从上位机或 C 客户端调用

下位机服务端直接实现生成头文件中声明的 `server_on_add` 函数（返回 `0` 发送响应结构体，返回非零 `int32_t` 发送 16 字节紧凑业务拒绝帧）：

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

当服务端运行起来后（完整双端联调见 [UDP 双端示例](examples/05_zephyr_udp/) 与 [SDK 打包示例](examples/06_bindings/)），上位机 Python 客户端即可像调用本地函数一样调用下位机，并直接将下位机的非零拒绝码捕获为 `RejectedError` 异常：

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

如果调用方也是 C11 节点（如 [`examples/01_rpc/client_async.c`](examples/01_rpc/client_async.c)），客户端无需阻塞等待，可连续并发发起最多 4 个在途 RPC 请求（请求结构体在返回 `WL_OK` 前已编码快照进节点内部，局部变量可立即销毁）：

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

主循环（Single Owner）只需周期性调用 `<node>_step(&node, now_ms)` 即可统一驱动解包、回调、超时检查与发送。

---

## 附：30 秒免安装 WLC 验证本地 C 工具链

仓库已附带一份预生成的 C11 节点代码（[`examples/endpoint/generated/`](examples/endpoint/generated/)）。只要有 CMake 3.21+ 与 C11/C++20 编译器，即可直接编译并运行双端内存回环测试：

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
./build/core/examples/wirelink_bare_metal_loopback
```

终端输出：

```text
6 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent
```

---

## 实测静态体积：完整通信装配账本

基于 GCC 16.2.1（`-std=c11 -Os -ffunction-sections -fdata-sections`）：
- **核心库代码体积**：`src/*.c` 全部 12 个目标文件未裁剪总和为 `.text = 17,474 B`、`.rodata = 1,524 B`、`.data = 0 B`、`.bss = 4 B`（实际链接固件开启 `--gc-sections` 后会自动剔除未使用的流分帧器与辅助模块）。
- **完整双向节点装配总 RAM**：将节点结构体（双向 `demo_node_t` 624 B，其中 `wl_endpoint_t` 440 B、`wl_packet_port_t` 128 B）、双 RX 接收缓冲以及流连接器/FIFO 全部计入后：

| 完整通信装配方案 | 实测总 RAM | 包含的静态组件 |
| :--- | ---: | :--- |
| `native` 分帧装配 | **790 B** | 节点结构体 + 端口 + 双 RX 接收槽 |
| `length16` 分帧装配 | **932 B** | 上述基础装配 + `length16` 流连接器与拼包缓冲 |
| `cobs_crc16` 串口装配 | **1,054 B** | 上述基础装配 + COBS/CRC16 流连接器 + 字节 FIFO |

单向或单角色节点（如只发遥测的传感器）会自动省去未声明方向的请求、回复或消息存储池。复现命令与统计口径见 [Footprint 测量指南](benchmarks/footprint/README-cn.md)。

---

## 渐进式教程与专题文档导航

- **[在线交互式教程（第 01 ~ 06 课）](site/src/content/docs/zh-cn/learn/)**：
  1. [01 编写 .wl 并打通上下位机通信](site/src/content/docs/zh-cn/learn/01-loopback-endpoint.mdx)
  2. [02 单向遥测（Telemetry）与双向 RPC 的协同](site/src/content/docs/zh-cn/learn/02-telemetry.mdx)
  3. [03 打造真实协议：字符串、可选字段与内存生命周期](site/src/content/docs/zh-cn/learn/03-rpc-basics.mdx)
  4. [04 接上真实物理链路：UART 串口与 UDP 上位机联调](site/src/content/docs/zh-cn/learn/04-strings-and-lifetime.mdx)
  5. [05 并发调用、超时取消与发送背压控制](site/src/content/docs/zh-cn/learn/05-concurrency-backpressure.mdx)
  6. [06 进阶内核机制：安全停机、RTOS 唤醒与零拷贝 DMA](site/src/content/docs/zh-cn/learn/06-host-sdk-and-transports.mdx)
- **[WLC 语言、CLI 与生成 API 手册](wlc/README.md)**：`.wl` 类型系统、`node` 配置、生成的 C11 函数签名与 CMake 集成。
- **[驱动移植指南（UART / USB / UDP / SPI）](docs/porting-adapters.md)**：默认 `embedded` 同步整帧接入模板与 `external` 异步零拷贝 DMA 租约协议。
- **[Wire v2 二进制协议规范](docs/wire-format.md)**：4B 帧头、8/12B RPC 子头、TLV / Positional-v1 与 COBS+CRC16 字节图解。
- **[开发与验证指南](docs/development.md)**：目录职责、测试矩阵、Zephyr Twister、libFuzzer 与体积验收命令。
- **[可编译教学示例 (`examples/`)](examples/)** · **[Zephyr 工程示例 (`samples/zephyr/`)](samples/zephyr/)** · **[基准测试 (`benchmarks/`)](benchmarks/)**

[Apache-2.0 许可证](LICENSE)
