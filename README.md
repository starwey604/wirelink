# Wirelink

[English](README-en.md) · [工程文档](docs/README.md) · [WLC 编译器手册](wlc/README.md) · [在线交互式文档](https://docs.silkenkite.ink/wirelink/)

面向嵌入式 MCU（裸机 / RTOS / Zephyr）与上位机的 **纯 ISO C11、零堆分配（0 `malloc`）** 通信中间件。

告别手写串口拆包状态机、字节序转换与 RPC 超时定时器：只需写一份 `.wl` 文件声明消息、RPC 与节点角色，即可自动生成一对干净的 `.h/.c` 固件节点代码，以及带完整类型提示的 **Python / C++20 上位机客户端 SDK**。

- **纯粹的单线程 `step(now)` 核心**：核心库无 OS、线程、锁或系统时钟依赖；主循环传入当前毫秒时间戳并步进，中断（ISR）与 DMA 仅操作无锁传输边界。
- **清爽的业务结构体（默认必选）**：字段默认必选，直接读写 `msg.temperature`，告别满屏的 `has_xxx = true`；只有显式声明 `optional` 的字段才带存在位。
- **按角色精确裁剪的固定内存**：默认节点内嵌端口与定长收发缓冲（最多 4 路并发 RPC、3 个待发回复、1 个普通消息快照槽、8 项 TX 调度队列）。未声明的角色分配 **0 字节**；槽位暂满时显式返回 `WL_ERR_WOULD_BLOCK`。
- **正交的 Wire v2 线缆与分帧**：4 字节小端帧头 + 8/12 字节定长 RPC 子头；普通消息可选支持演进的 `schema_v1` (TLV) 或零 Tag 开销的 `positional_v1`；一行 `framing = cobs_crc16;` 即可无缝跑在 UART 串口字节流上。

> 当前本地候选版本配对：**Wirelink 0.14.0 / WLC 0.14.0 / Codegen 0.14 / SDK Binding API 3**。从旧版升级请重新生成 C 与 SDK，详见 [0.14 迁移说明](docs/release-v0.14.0-cn.md)。

---

## 30 秒本地体验（无需安装 WLC 或硬件）

仓库已附带预生成的 C11 节点代码（[`examples/kernel/generated/`](examples/kernel/generated/)）。只要有 CMake 3.21+ 与 C11/C++20 编译器，即可直接编译并运行双端内存回环：

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

## 三步跑通你的第一个 RPC 服务

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

节点默认采用 `io = embedded; handlers = static; framing = native; codec = schema_v1; timeout_ms = 500;`，无需额外编写端口装配代码或回调函数注册表。

### 第二步：按角色生成一对 C11 文件

```sh
# 1. 在仓库根目录构建本地 WLC 编译器
cargo build --release --locked --manifest-path wlc/Cargo.toml

# 2. 为单端固件生成（例如只部署服务端时用 --node server，绝不会引入客户端符号）
wlc/target/release/wlc compile examples/01_rpc/calculator.wl \
  --node server --out-dir build/server

# 3. 若在同一个进程内同时运行 client 和 server 做回环测试，则用 --all-nodes
wlc/target/release/wlc compile examples/01_rpc/calculator.wl \
  --all-nodes --out-dir build/calculator
```

*(提示：在 CMake 工程中可直接使用 `wirelink_wlc_generate(... NODE server)` 或 `ALL_NODES` 自动管理生成与依赖追踪。)*

### 第三步：实现静态业务函数与收发钩子，驱动 `step(now)`

服务端直接实现生成头文件中声明的 `server_on_add` 函数（返回 `0` 发送响应，返回非零 `int32_t` 发送 16 字节紧凑业务拒绝帧）：

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

在 [`examples/01_rpc/client.c`](examples/01_rpc/client.c) 的回环中，发送钩子 `client_tx_write()` 把整帧字节直接交给对端的 `server_rx_packet()`。调用 `client_init(&caller)` 与 `server_init(&callee)` 后，客户端即可并发发起最多 4 个在途 RPC 请求（请求结构体在返回 `WL_OK` 前已编码快照进节点，局部变量可立即销毁）：

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

主循环（Single Owner）只需持续调用 `client_step(&caller, now_ms)` 与 `server_step(&callee, now_ms)` 推进收发与回调：
- **超时起算**：免时间戳的简化调用 `client_call_add` 从**下一次 `step(now)`** 开始计时；需要显式传入发起时刻、自定义超时、独立 `user` 上下文或获取取消句柄时，使用 `client_call_add_ex()`。
- **物理收发与停机契约**：`embedded` 发送钩子必须整帧同步接受或整帧拒绝（返回 `WL_ERR_WOULD_BLOCK`，待外设恢复可写时调用 `<node>_tx_ready()`）；异步零拷贝 DMA 驱动请选择 `io = external`，详见 [驱动移植指南](docs/porting-adapters.md)。停机前先停止 RX 源头，再调用 `<node>_close()` 并继续步进至 `<node>_quiescent()` 为真。

### 第四步：一键生成上位机 Python / C++20 SDK

```sh
wlc/target/release/wlc sdk examples/01_rpc/calculator.wl \
  --node client --name calculator --out-dir build/calculator-sdk
```

生成的 SDK 工程自带 C++20 客户端与带 `.pyi` 类型提示的 Python 3.10+ UDP 客户端包（完整联调见 [UDP 双端示例](examples/05_zephyr_udp/) 与 [SDK 打包示例](examples/06_bindings/)）。在 Python 中调用远端服务并捕获业务拒绝异常：

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

---

## 实测静态体积：测量完整通信装配，而非单个结构体

基于本机 GCC 16.2.1（`-std=c11 -Os -ffunction-sections -fdata-sections`）：
- **核心库代码体积**：`src/*.c` 全部 12 个目标文件未裁剪总和为 `.text = 17,474 B`、`.rodata = 1,524 B`、`.data = 0 B`、`.bss = 4 B`（这唯一的 4 B `.bss` 是进程级自动实例号计数器；实际链接固件开启 `--gc-sections` 后会自动剔除未使用的流分帧器与模块）。
- **完整节点装配总 RAM**：将节点结构体（双向 `demo_node_t` 624 B，其中 `wl_kernel_t` 440 B、`wl_packet_port_t` 128 B）、双 RX 接收缓冲以及流连接器/FIFO 全部计入后：

| 同一双向 Demo 完整静态装配 | 0.13 基线总 RAM | 当前 0.14 总 RAM | 变化 |
| :--- | ---: | ---: | ---: |
| `native` 分帧装配 | 782 B | **790 B** | +8 B |
| `length16` 分帧装配 | 924 B | **932 B** | +8 B |
| `cobs_crc16` 串口装配（含字节 FIFO） | 1,046 B | **1,054 B** | +8 B |

单向或单角色节点（如只发遥测的传感器）会自动省去未声明方向的请求、回复或消息存储池。复现命令与统计口径见 [Footprint 测量指南](benchmarks/footprint/README-cn.md)。

---

## 导航与深入阅读

- **[在线交互式教程（01 ~ 06 课）](site/src/content/docs/zh-cn/learn/)**：从零掌握回环、单向遥测、RPC、字符串生命周期、并发背压与上位机 SDK。
- **[WLC 语言、CLI 与生成 API 手册](wlc/README.md)**：默认必选与 `optional`、节点配置、静态/动态 Handler 与 CMake 集成。
- **[Wire v2 二进制协议规范](docs/wire-format.md)**：4B 帧头、8/12B RPC 子头、TLV / Positional-v1 与 COBS+CRC16 字节图解。
- **[驱动移植指南（UART / USB / UDP / SPI）](docs/porting-adapters.md)**：`embedded` 同步整帧接入模板与 `external` 异步零拷贝租约协议。
- **[开发与验证指南](docs/development.md)**：目录职责、测试矩阵、Zephyr Twister、libFuzzer 与体积验收命令。
- **[可编译教学示例 (`examples/`)](examples/)** · **[Zephyr 工程示例 (`samples/zephyr/`)](samples/zephyr/)** · **[基准测试 (`benchmarks/`)](benchmarks/)**

[Apache-2.0 许可证](LICENSE)
