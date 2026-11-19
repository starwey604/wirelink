# Wire v2 与二进制编码规范

[返回文档总览](README.md) · [传输层移植指南](porting-adapters.md) · 头文件对照：[`wire.h`](../include/wirelink/wire.h) · [`codec.h`](../include/wirelink/codec.h)

Wirelink 的线协议分层清晰：**规范帧（Canonical Frame）**面向包边界直接解析；字节流传输（UART/串行总线）只需在规范帧外侧包裹一层轻量化连接器（`COBS+CRC16` 或 `Length16`），内层帧头与 Payload 格式完全不变。

---

## 一、全局小端 Wire v2 帧结构

每个规范帧固定由 **4 字节小端帧头**（`WL_WIRE_NATIVE_HEADER = 4`）和 **Payload**（`0..2048` 字节，`WL_WIRE_MAX_PAYLOAD = 2048`）组成，最大规范帧长度为 **2052 字节**：

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| magic = 0x57  |     kind      |        route_id (uint16 LE)   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                Payload (0 .. 2048 bytes)                      |
|     - kind=0: 直接为消息体 (TLV 或 Positional-v1)              |
|     - kind=1/2/3: 先接 RPC 子头 (8B 或 12B)，再接消息体       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| 字段 | 偏移 | 长度 | 值与约束 |
| --- | --- | --- | --- |
| `magic` | 0 | 1 B | 固定魔数 `WL_WIRE_MAGIC = 0x57`（ASCII `'W'`），不匹配返回 `WL_ERR_BAD_FRAME` |
| `kind` | 1 | 1 B | `0 = WL_WIRE_MSG`（普通单向消息）、`1 = WL_WIRE_RPC_REQ`、`2 = WL_WIRE_RPC_RESP`、`3 = WL_WIRE_RPC_ERR`，越界返回 `WL_ERR_NOT_SUPPORTED` |
| `route_id` | 2 | 2 B | **小端** `uint16_t`，来自 `.wl` 消息的 `@id(...)`（必须 `!= 0`，`0` 返回 `WL_ERR_BAD_FRAME`） |

---

## 二、RPC 定长小端子头（`kind = 1, 2, 3`）

Wire v2 取消了旧版变长 `uint64` 会话编码，所有 RPC 字段均为 **32 位小端整数**，通过 `wl_wire_get_u32` / `wl_wire_put_u32` 读写；输入缓冲不要求自然对齐：

### 1. 请求与成功响应（`WL_WIRE_RPC_REQ = 1`, `WL_WIRE_RPC_RESP = 2`，8 字节）

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       session (uint32 LE)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      operation (uint32 LE)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|              Encoded Request / Response Message Body          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 2. 业务拒绝响应（`WL_WIRE_RPC_ERR = 3`，固定 12 字节，无消息体）

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       session (uint32 LE)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      operation (uint32 LE)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       status (int32 LE)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- `session` 与 `operation` 必须均 **非零**（`0` 返回 `WL_ERR_CORRUPT_PAYLOAD`）。
- 当服务端 Handler 返回非零 `int32_t` 业务状态码时，内核发送 `WL_WIRE_RPC_ERR`，Payload 总长严格等于 `WL_WIRE_RPC_HEADER_ERR = 12` 字节，无额外消息体；`status == 0` 视为畸形包。
- 请求方回调通过 `wl_rpc_result_t` 精确区分协议层结果：
  - `WL_RPC_SUCCESS`：收到成功响应（`status == 0`），解码后的响应体有效至回调返回前。
  - `WL_RPC_REJECTED`：收到对端业务拒绝（`result.status` 即对端返回的非零 `int32_t`）。
  - `WL_RPC_TIMED_OUT` / `WL_RPC_CANCELLED` / `WL_RPC_LOCAL_ERROR` / `WL_RPC_CLOSED`：本地超时、取消、I/O 失败或端点关闭（`result.status` 为本地 `wl_err_t`）。

---

## 三、消息体编码：Schema-v1 TLV 与 Positional-v1

消息体编码由共享 C11 运行时 [`src/codec.c`](../src/codec.c) 与 `wlc` 生成的描述符/窄类型特化共同驱动。

### 1. Schema-v1 TLV（默认编码，支持前后向演进）

每个字段以 `tag` 开头：

```text
tag = varint( (field_number << 3) | wire_type )
```

| `wire_type` | 含义 | 适用 `.wl` 类型 | 编码细则 |
| :---: | --- | --- | --- |
| `0` | Varint | `bool`, `uint32`, `uint64`, `int32`, `sint32`, `int64`, `sint64`, `enum` | unsigned 用 LEB128；**所有有符号整数与 `enum` 均先做 ZigZag 映射后再编码 Varint** |
| `1` | 64-bit | `fixed64`, `double` (`float64`) | 固定 **8 字节小端**（`wl_codec_put_le64`） |
| `2` | Length-delimited | `string`, `bytes`, `string<N>`, `bytes<N>`, 嵌套 `message`, `packed` 数组 | 先编码 `varint(byte_len)`，再紧跟原始字节；`string` 解码时强制执行 RFC-3629 UTF-8 校验 |
| `5` | 32-bit | `fixed32`, `float` (`float32`) | 固定 **4 字节小端**（`wl_codec_put_le32`） |

- 不接受非规范冗长 Varint（前导高字节不能全 0，否则返回 `WL_CODEC_ERR_MALFORMED`）。
- 未知 `field_number` 允许按 `wire_type` 安全跳过（`wl_codec_skip`），保证字段演进兼容性。
- 字段默认必选，显式 `required` 含义相同；必选字段不生成 `has_*`。只有 `optional` / `optional packed` 使用存在位。必选字段缺失返回 `WL_CODEC_ERR_MISSING_REQUIRED_FIELD`；重复非 `repeated` 字段返回 `WL_CODEC_ERR_DUPLICATE_FIELD`。

### 2. Positional-v1（可选超紧凑定序编码，仅限普通消息路由）

当 `.wl` 节点设置 `codec = positional_v1`，或普通 `send` / `recv` 路由使用 `@codec(positional_v1)` 时，取消字段 Tag。RPC 不使用此编码：

```text
+------------------------------+------------------------------------------+
| Optional Presence Bitmap     | Present Fields in Declaration Order      |
| ceil(optional_count / 8) B   | (raw encoded values, no field tags)      |
+------------------------------+------------------------------------------+
```

- 存在位图仅按声明中 `optional` / `optional packed` 字段顺序从 Byte 0 的 LSB（bit 0）起排布；末尾未使用的填充 bit 必须为 `0`。
- 没有显式 optional 字段时，`presence_bytes = 0`，无位图开销。`packed` 默认必选；从旧版可选 packed 迁移时应写 `optional packed`。
- 两端需使用一致的字段顺序、类型与 optional 布局。默认必选是 schema 与 C API 规则的变化；没有新增 Wire v2 帧头版本。

---

## 四、流式连接器外层分帧

针对无天然包边界的流式物理链路（如 UART / RS-485 / SPI 字节流），在 [`packet_stream.h`](../include/wirelink/packet_stream.h) 中提供两种独立连接器，**所有校验与分帧与内层 `wl_endpoint_t` 完全正交**：

### 1. `framing = cobs_crc16`（COBS + CRC-16/CCITT-FALSE，UART 推荐）

```text
+-------------------------------------------------------+------+
| COBS Encode( Canonical Frame (4..2052 B) || CRC16_BE )| 0x00 |
+-------------------------------------------------------+------+
                                              ^ big-endian 2B
```

- **CRC 算法**：CRC-16/CCITT-FALSE（初值 `0xFFFF`，多项式 `0x1021`，无反转），以**大端 2 字节**附加在规范帧尾后一起进行 COBS 编码，确保线上传输的帧体中绝不出现 `0x00`。
- **帧界恢复**：发生字节丢失或 CRC 错误时，连接器返回 `WL_ERR_BAD_FRAME` / `WL_ERR_COBS_DECODE` / `WL_ERR_CRC`，并自动丢弃至下一个 `0x00` 定界符恢复后续包同步。
- **静态表开销**：`src/crc.c` 采用 16 项 nibble 查表，`.rodata` 仅 **32 字节**（恰好占用单条 32 B I-Cache Line）。
- **缓冲区上界**：TX/RX 物理缓冲至少需 `wl_packet_stream_cobs_crc16_bound(max_canonical_size)` 字节（当 `max = 2052` 时，最大 COBS 编码体含 `0x00` 尾符不超过 `2064` 字节）。

### 2. `framing = length16`（Length16 / NONE，干净硬件总线）

```text
+--------------------+----------------------------------+
| len (uint16 LE)    | Canonical Frame (len bytes)      |
+--------------------+----------------------------------+
```

- 前导 2 字节小端无符号长度 `len`（合法范围 `4 <= len <= maximum_rx_frame_size <= 2052`）。
- 无校验开销；若发生丢字节需由物理总线帧同步机制重启流连接器。
- **缓冲区上界**：`wl_packet_stream_length16_none_bound(size) = size + 2`。
