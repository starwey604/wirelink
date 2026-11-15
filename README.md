# Wirelink

[English Placeholder](README-en.md) · [工程文档](docs/README.md) · [WLC 编译器](wlc/README.md) · [在线文档](https://docs.silkenkite.ink/wirelink/)

面向嵌入式 MCU（裸机 / RTOS / Zephyr）与上位机通信的**纯 C11 零动态分配**通信中间件——写一份 `.wl` 消息定义，自动生成跨语言代码，用数行固定内存 API 打通 **UART / USB Bulk / UDP / SPI**。

- **严格 ISO C11 核心**：不依赖 `malloc`、线程、系统时钟或 OS 原语，静态区 `.data = 0`、`.bss = 0`。
- **单结构体直觉 API**：每条消息只生成单一 C 结构体 `<name>_t`，零 View/Owned Value 双套胶水、零编码回调函数指针。
- **小端 Wire v2 协议**：4 字节帧头 + 8/12 字节定长小端 RPC 头，支持 Schema-v1 TLV 与 Positional-v1 定序编码。
- **确定性内存与背压**：4 槽并发 RPC（`WL_KERNEL_CALLS = 4`）、3 槽回复（`WL_KERNEL_REPLIES = 3`）、8 槽循环 TX 队列，超限即返回 `WL_ERR_WOULD_BLOCK`。

---

## 三步完成端到端通信（The 3-Step Workflow）

### 第 1 步：定义消息结构（`.wl`）与链路绑定（`.bind.wl`）

`calculator.wl` 描述跨端数据结构；`calculator.bind.wl` 声明该部署开启哪些 RPC / 单向消息与物理通道边界（修改通道无需改动业务结构体）：

<!-- [docs:calculator_schema] -->
```wl
message AddRequest @id(20) {
  required int32 left @id(1);
  required int32 right @id(2);
}

message AddResponse @id(21) {
  required int32 sum @id(1);
}
```
<!-- [/docs:calculator_schema] -->

<!-- [docs:calculator_bind] -->
```wl
profile version 2;
endpoint { envelope=native_packet; }

rpc Add {
  request = AddRequest;
  response = AddResponse;
}
```
<!-- [/docs:calculator_bind] -->

### 第 2 步：运行 `wlc` 生成 MCU C11 端点与上位机 SDK

```sh
# 生成纯 C11 编解码器与类型化端点（calculator.h/.c, calculator_kernel.h/.c）
wlc compile calculator.wl --profile calculator.bind.wl --out-dir generated

# 一键生成跨平台 C++20 + Python 3.10+ 上位机 UDP SDK 包
wlc sdk     calculator.wl --profile calculator.bind.wl --name calculator --out-dir sdk
```

### 第 3 步：MCU 端 C11 与上位机 Python 联调

左/上为 MCU 端处理与发包代码，右/下为上位机 Python 调用（两段代码均直接取自可编译运行示例）：

**MCU 端（纯 C11 RPC Handler & 遥测发送）：**

<!-- [docs:calculator_server] -->
```c
int32_t calculator_add(void *user, const add_request_t *request, add_response_t *response) {
  (void)user;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true; response->sum = (int32_t)sum;
  return 0;
}
```
<!-- [/docs:calculator_server] -->

<!-- [docs:telemetry_publish] -->
```c
const telemetry_kernel_handlers_t handlers = {.on_message_telemetry = telemetry_receive};
for (unsigned side = 0; side < 2; ++side)
  EXAMPLE_REQUIRE(telemetry_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
for (unsigned sample = 0; sample < 12; ++sample) {
  telemetry_t message = {.has_sample = true, .sample = sample,
    .has_temperature_centi_c = true, .temperature_centi_c = -1234};
  EXAMPLE_REQUIRE(telemetry_kernel_send_telemetry(&endpoints[0], &message) == WL_OK);
  memset(&message, 0, sizeof(message)); /* Admission copied the source. */
  example_pair_wait(&pair, &telemetry_received, sample + 1);
}
```
<!-- [/docs:telemetry_publish] -->

**上位机端（Python SDK 同步调用与业务错误捕获）：**

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

## 实测静态体积与核心保证

下表数据由 [`benchmarks/footprint/footprint.py`](benchmarks/footprint/footprint.py) 在当前 WP-4 核心上使用 `-std=c11 -Os -ffunction-sections -fdata-sections` 实测得出（零动态内存分配，全局 `.data = 0 B`、`.bss = 0 B`）：

| 模块 / 结构体 | `.text` (B) | `.rodata` (B) | 静态 RAM 占用 | 职责说明 |
| --- | ---: | ---: | --- | --- |
| `src/kernel.c` | 4,384 | 0 | `wl_kernel_t`: 440 B | 微内核状态机、8 槽 TX 队列、4 槽并发 RPC、超时与取消 |
| `src/packet_port.c` | 1,315 | 0 | `wl_packet_port_t`: 128 B | 2 槽原子 RX 交接、TX 物理租约令牌 |
| `src/codec.c` | 6,202 | 232 | 0 B（无全局状态） | 共享 TLV 与 Positional-v1 运行时、ZigZag、UTF-8 校验 |
| `src/types.c` | 244 | 1,260 | 0 B | `wl_err_str()` 错误码字符串映射 |
| `src/cobs_crc16_stream.c` + `cobs.c` + `crc.c` | 2,184 | 32 | `wl_packet_stream_t`: 120 B | UART/串口 COBS 分隔与 CRC-16/CCITT-FALSE（32 B nibble 表） |
| `src/length16_none_stream.c` + `packet_stream.c` | 1,158 | 0 | 共用 `wl_packet_stream_t` | 2 字节小端长度前缀流式连接器 |
| `src/byte_fifo.c` + `byte_fifo_cobs.c` | 767 | 0 | `wl_byte_fifo_t`: 72 B | ISR 到 Owner 的无锁 SPSC 字节暂存 |
| `src/packet_loopback.c` | 727 | 0 | `wl_packet_loopback_t`: 136 B | 主机/单元测试用双端内存回环传输 |
| **全核心 12 个目标文件合计** | **16,981** | **1,524** | `demo_kernel_t`: **616 B** | 实际链接时通过 `--gc-sections` 仅保留所用传输与消息路由 |

> [!NOTE]
> 版本兼容基线：Wirelink **0.13.0** · WLC **0.13.0** · Codegen 契约 **0.13** · SDK Binding API **3** · Binding Profile **v2**。

---

## 30 秒本地体验（无需 WLC 或硬件）

仓库已包含预生成的确定性示例 C 代码（`examples/kernel/generated/`），只需 C11 编译器与 CMake 3.21+ 即可编译并运行双端双向回环示例：

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
./build/core/examples/wirelink_bare_metal_loopback
```

运行后预期输出：

```text
6 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent
```

---

## 文档导航与深入阅读

| 文档 / 目录 | 内容 |
| --- | --- |
| [docs/README.md](docs/README.md) | 中文文档总览与快速入口 |
| [docs/wire-format.md](docs/wire-format.md) | Wire v2 帧头、RPC 子头、TLV / Positional-v1 与 COBS+CRC16 字节级规范 |
| [docs/porting-adapters.md](docs/porting-adapters.md) | 自定义 UART DMA / USB Bulk / SPI / UDP 驱动接入 `wl_packet_port_t` 指南 |
| [docs/development.md](docs/development.md) | 仓库目录、CMake / Cargo / Zephyr Twister 构建与全量测试矩阵 |
| [wlc/README.md](wlc/README.md) | Schema 编译器 `wlc` 使用说明、CLI 子命令与代码生成契约 |
| [examples/](examples/) | 完整可编译示例（遥测、RPC、异步取消、设备服务、UDP 联调、Python/C++ SDK） |
| [samples/zephyr/](samples/zephyr/) | Zephyr RTOS 平台示例（UART DMA、USB Bulk、UDP Peer、多服务组合） |
| [benchmarks/](benchmarks/) | 静态体积测算与嵌入式/主机微基准套件 |

[Apache-2.0 许可证](LICENSE) · [English Placeholder](README-en.md)
