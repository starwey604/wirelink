# Packet 与 RPC wire 格式

[English](packet-protocol.md) · [Kernel API](kernel-api-cn.md) · [选择 connector](link-profiles-cn.md)

解码采集数据时，先确认 connector，恢复规范 DATA 包，读取消息 ID，再选择该 route 的 payload codec。DATA 头和业务 body 都不携带 codec discriminator。两端通过带外方式约定 connector、schema、ID 和 codec。

## 规范 DATA 包

kernel 接受普通 Compact-v1 DATA：四字节 native/NONE 头加 `0..2048` 字节 payload。

| Offset | 编码 |
| --- | --- |
| `0` | `0xA4`：marker `0xA0`，bit `3..2` 为 version `1`，bit `1..0` 为普通 DATA kind `0` |
| `1` | 保留，必须为 `0x00` |
| `2..3` | 非零 `uint16_t` 消息 ID，大端 |
| `4..end` | 普通业务 body，或 RPC metadata 加业务 body |

长度由传输包边界确定。native/NONE 不添加 length、CRC 或 COBS 字节，最大规范帧为 `2052` 字节。RPC metadata 也计入 `2048` 字节 payload 上限。

[`native_none.c`](../src/native_none.c)验证前缀。短输入、错误 marker/保留字节、零 ID 返回 `WL_ERR_BAD_FRAME`；不兼容 version 返回 `WL_ERR_PROTOCOL_VERSION`；不支持的低位 kind 返回 `WL_ERR_NOT_SUPPORTED`；普通帧超长返回 `WL_ERR_FRAME_TOO_LONG`。`seal` 只写包头，payload 超长时返回 `WL_ERR_PAYLOAD_TOO_LONG`。read 失败保持输出不变。

消息 ID `0x1234` 的前缀为 `a4 00 12 34`。[`kernel_wire` fixture](../tests/core/kernel_wire.c)对直到 `2048` 的 payload 长度验证该前缀，并检查错误/不支持的包。拒绝一个包不妨碍后续有效包被处理。独立[冻结 vector](conformance-v1.md)还覆盖 standalone frame tool；kernel 接受普通 DATA，不执行 ACK/retry。

## Managed RPC metadata

请求与响应使用 profile 分配的不同 ID。RPC 在 DATA payload 起始处使用 managed v4 metadata：

| 字段 | 编码 |
| --- | --- |
| Client session | 非零 `uint64_t`，8 字节大端；客户端通信 instance |
| Operation | 非零 `uint32_t`，1–5 个 tagged operation 字节 |
| Business status | 仅拒绝响应存在：非零有符号 `int32_t`，4 字节大端补码 bits |
| Business body | 请求/成功响应：managed tagged codec；拒绝响应：不存在 |

operation 首字节的 bit `0` 为 rejection，bit `6..1` 保存 operation 的 bit `5..0`，bit `7` 表示 continuation。后续字节从低到高每组携带七个 operation bit，bit `7` 表示 continuation。第五个字节只能使用五个 payload bit。编码必须最短：冗余零组、overflow、truncation、零 operation/session、零 status 的拒绝响应均无效。请求不能是拒绝响应。拒绝 status 必须占用完整的剩余四字节，后面没有 body 或尾随字节。

请求/成功响应的 metadata 为 `9..13` 字节，拒绝响应为 `13..17` 字节。`WL_KERNEL_RPC_HEADER_MAX` 为 `17`。以下 metadata-only vector 使用 session `0x123456789abcdef0`，字节为 `12 34 56 78 9a bc de f0`：

| Operation / status | Session 后字节 | Metadata 总字节数 |
| --- | --- | ---: |
| `1 / 0` | `02` | 9 |
| `63 / 0` | `7e` | 9 |
| `64 / 0` | `80 01` | 10 |
| `1 / -1` | `03 ff ff ff ff` | 13 |
| `UINT32_MAX / 0` | `fe ff ff ff 1f` | 13 |

完整系列包含 `INT32_MIN`/`INT32_MAX` 拒绝 status，见 [`rpc_vectors.h`](../tests/core/rpc_vectors.h)，由 `kernel_wire` 执行。[`rpc_wire.c`](../src/rpc_wire.c)实现 [`kernel_wire.h`](../include/wirelink/kernel_wire.h)中的公开原语。

服务端原样回传 session/operation。客户端用响应 ID 加 instance/operation 匹配 waiting 且已提交的调用。此身份不是认证或应用幂等键。kernel 不自动重试或抑制重复，独立接纳的重复请求可以再次执行。timeout/cancel 与副作用见 [RPC 使用](tutorial-rpc-cn.md)，本地错误见 [API 结果](kernel-api-cn.md#terminal-results)。

## 显式选择业务 body codec

schema 定义字段和值域，binding 选择 wire 表示，与 connector 分开：

| 用途 | Profile codec | Wire identity / 形态 |
| --- | --- | --- |
| 普通消息 | `schema_v1`，默认 | `schema-v1`：tagged field |
| 普通消息 | `positional_v1` | `positional-v1`：optional bitmap，field-ID 顺序 |
| RPC 请求 / 成功响应 | `managed_tagged`，默认 | `managed-rpc-v4-tagged-varuint32`：上述 metadata 加 tagged body |

profile v2 要求 managed tagged RPC，拒绝 positional 和 mapped RPC。独立公开 codec 能描述的形态多于有界 kernel route，不能仅从 schema 语法推断 endpoint 支持范围。

### Tagged field 与标量值域

两种 tagged body 都使用 unsigned LEB128 key `(field_number << 3) | wire_type`，字段号为 `1..65535`。wire type 为 `0` varint、`1` 八字节 fixed、`2` length-delimited、`5` 四字节 fixed。encoder 按 ID 排序；decoder 接受已知字段重排、跳过允许的未知字段，拒绝重复 singular/packed 字段或已知字段的错误 wire type。重新编码不保留跳过的未知数据。

`bool` 只接受 `0/1`。无符号整数保持声明的 8/16/32/64-bit 全值域，有符号整数使用全值域 ZigZag 和最短 unsigned LEB128；窄类型 overflow 报错，不截断。enum 保持开放的有符号 `int32_t` 值域，包括未声明的值。`fixed64` 是八字节大端。关键区别是 **scalar `fixed32`**：

- 公开 Schema-v1：wire type `5`，四字节大端。字段 `1`、值 `128` 编码为 `0d 00 00 00 80`。
- Managed tagged：wire type `0`，全值域最短 unsigned varint。同一字段/值编码为 `08 80 01`。

`float32`/`float64` 按大端排列 IEEE binary32/binary64 bits。codec 复制 bits，不执行数值归一化，保留 signed zero、infinity 和 NaN payload。packed numeric array 使用 length-delimited、精确元素数量、大端固定宽度元素；两种格式的 packed `fixed32` 都如此。

### Presence、字符串与有界形态

optional 保留 `has_*`：缺失时使用本地 default/zero，presence 为假；即使值等于 default，显式存在时 presence 仍为真。required 必须存在。string 是 length-delimited 的有效 UTF-8，上界计编码字节，不计字符或额外终止符；允许内嵌 NUL，并计入 length。bytes 携带任意数据。view 借用输入，在 kernel 回调返回前使用 owned conversion 复制所需结果。

tagged codec 支持无环 nested message，最大 nesting depth 为 `8`。公开 Schema-v1 支持 general repeated，使用调用方提供的 array/count/capacity；它的出现次数没有 schema 上界，因此不存在有限 endpoint 存储上限。kernel 所选消息要求有限 maximum：不能包含 general repeated 或无界 string/bytes，嵌套内部也不能包含。packet 容量内支持有界 string/bytes、有界 nested message 和 fixed-count packed array。

普通 Positional-v1 支持 flat scalar 和 fixed-count packed，不支持 nested/repeated/string/bytes，不带 tag 或未知字段跳过，拒绝尾随字节；其 scalar `fixed32` 也用 varint。optional bit 和精确 schema 配对很重要，见 [Positional-v1](positional-v1.md)。[Schema-v1 参考](schema-v1-cn.md)定义公开 tagged 格式。WLC 的 `src/codec_plan.rs`、`src/kernel_profile.rs`、`src/kernel_codegen.rs` 定义选择规则和有限 route 容量。

## Connector 字节与容量

设规范帧长度为 `N`，包含四字节头：

- **native/NONE：**以一个包发送 `N` 字节。
- **COBS/CRC16：**对全部 `N` 字节计算 CRC16-CCITT-FALSE，polynomial `0x1021`、initial `0xffff`、不 reflection、final XOR `0`；以大端附加两个 CRC 字节，再 COBS 编码，最后加 delimiter `00`。最坏 wire 长度为 `N + 2 + floor((N + 2) / 254) + 2`。
- **length16/NONE：**在包前添加两字节大端长度 `N`，wire 长度为 `N + 2`，长度值不含自身前缀。ID 为 `0x1234` 的空 DATA 包为 `00 04 a4 00 12 34`。

`N=2052` 时，三种上界分别为 `2052`、`2064`、`2054`。使用选定 connector 的 bound 函数/生成 macro，不要使用包含其他 packet kind 的 frame-tool 常量。TX scratch 与每个 stream RX unit 必须分别覆盖其配置 wire bound。profile、默认 RX limit 推导和物理 USB headroom 见[容量选择](link-profiles-cn.md)及 [adapter 配置](adapters-cn.md)。

COBS feed 在压力下保留未消费后缀；通过 delimiter 恢复时丢弃错误/超长 unit，允许后续 unit 继续。length16 拒绝小于四字节或大于配置 canonical RX limit 的长度，并丢弃该声明长度的 unit；任意字节丢失后，它没有 COBS 式重新同步 delimiter。两种 connector 都在 commit 前验证规范 DATA。[`packet_stream` 测试](../tests/core/packet_stream.c)对所有 `0..2048` payload 长度与独立 frame tool 比较编码字节，并覆盖 partial input、压力和错误 unit。

使用[开发指南](development-cn.md)的 core 构建运行 wire 与 connector fixture：

```sh
ctest --test-dir /tmp/wirelink-dev-core --output-on-failure \
  -R '^(kernel_wire|packet_stream)$'
```
