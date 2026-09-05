# WLC Schema 与 Payload Format v1

状态：1.0 前冻结的 baseline，包含 required field、exact-width integer、dense numeric
array 和有界借用字段。英文版 [`schema-v1.md`](schema-v1.md) 是规范来源。

本文定义 Wirelink `DATA` 包承载的应用 payload，独立于 link header、COBS envelope、
integrity trailer 和 link ACK。生成 codec 不负责重传，也不会把链路 ACK 当作应用结果。

## 1. Schema 源码

payload 编码仍为 v1。新源码推荐用 `@id(n)` 显式标记编号；已有 schema 的 `= n`
写法继续支持：

```text
schema         = "version" positive-integer ";" item+
item           = declaration | reservation
declaration    = message | enum
reservation    = "reserved" positive-integer ";"
id-attribute   = "@" "id" "(" positive-integer ")" | "=" positive-integer
message        = "message" identifier id-attribute
                 "{" (field | reservation)* "}"
field          = optional-field | required-field | repeated-field
                 | packed-field | required-packed-field
optional-field = "optional" type identifier id-attribute
                 ("[" "default" "=" literal "]")? ";"
required-field = "required" type identifier id-attribute ";"
repeated-field = "repeated" type identifier id-attribute ";"
packed-field   = "packed" packed-type identifier "[" positive-integer "]"
                 id-attribute ";"
required-packed-field = "required" "packed" packed-type identifier
                        "[" positive-integer "]" id-attribute ";"
type            = identifier | bounded-length-type
bounded-length-type = ("bytes" | "string") "<" positive-integer ">"
packed-type    = "float32" | "float64" | "fixed32" | "fixed64"
enum           = "enum" identifier id-attribute "{" enum-item* "}"
```

`@id(n)` 用于 message/enum 声明编号和字段编号；枚举值仍写 `NAME = integer;`，
可选字段默认值仍写 `[default = literal]`。两种编号拼法产生相同的语义模型、标识摘要、
生成 C 和编码字节；仅修改拼法不需要递增 schema revision。编号不会按声明顺序自动分配。

v1 没有隐式 ID、map、`oneof`、service、extension 或 schema 级 reliability。可靠性由
binding profile 或发送 API 选择。packed 是固定数量 numeric array，不是通用 repeated。
required 不能带 default 或 repeated；固定数量必填 vector 使用 `required packed`。

`version` 只用于 WLC 兼容性检查，不是 Wirelink packet version，也不写入 payload。
首版为 `version 1`；兼容修改递增 revision，并对照前一 semantic model。

message 的非零 16-bit declaration ID 就是 DATA header 的 `message_id`。enum declaration
ID 只是稳定 compiler symbol，不是 packet message ID。field number 为 message-local
非零 16-bit。删除的 message、enum、field、enum value 永久 reserved。name、ID、field
type/cardinality 不能兼容修改；packed count、整数宽度/符号和 string/bytes bound 都属于
wire identity。default 只影响本地 decode。向既有 message 新增 required field 不兼容。

### 1.1 内建类型

| 类型 | C 表示 | Wire 表示 |
| --- | --- | --- |
| `bool` | `bool` | 无符号 varint，仅 `0`/`1` |
| `uint8`…`uint64` | 精确宽度无符号整数 | unsigned LEB128 |
| `int8`…`int64` | 精确宽度有符号整数 | ZigZag 后 unsigned LEB128 |
| `fixed32`、`fixed64` | `uint32_t`、`uint64_t` | 4/8 bytes，大端 |
| `float32`、`float64` | `float`、`double` | IEEE-754 bits，4/8 bytes，大端 |
| `bytes` | `wl_codec_bytes_t` | length-delimited bytes |
| `string` | `wl_codec_string_t` | length-delimited，有效 UTF-8 |

`bytes<MAX>`/`string<MAX>` 的 `MAX` 为 1…65535，按编码字节计数，不含 NUL。encode 前和
decode length 后立即验证；越界为 `WL_CODEC_ERR_INVALID_VALUE`，UTF-8 错误仍为
`WL_CODEC_ERR_UTF8`。decode 继续 zero-copy，只产生借用 view。

enum 按 `int32` ZigZag varint 编码，生成 integer typedef 与常量而不是 C `enum`，因此旧
receiver 仍可表示未知新值。嵌套 message 使用 length-delimited 完整 message body；直接/
间接递归被拒绝，最大 nesting depth 为 8。出现 float 时，生成 header 静态要求 IEEE
binary32/binary64，使用 `memcpy` 搬运 bits，保证 signed zero、infinity、NaN payload
精确 round-trip。

`bytes` 无 default；string default 必须是 UTF-8。整数、bool、fixed、enum default 必须
落在类型范围，enum 必须对应已有数字值。v1 暂不接受显式 float default；缺失 float 清为
正零。repeated/packed 无 default。有界 string default 在分析时按 UTF-8 字节检查。

## 2. Wire Format

业务 codec 的 payload 是零个或多个 tagged field，没有 payload header 或 schema revision；
DATA `message_id` 选择 decoder。托管 RPC 会在这些字节前添加自己的通信元数据，接收时
先由 RPC runtime 取出，再调用 codec；见 [RPC runtime](rpc-runtime-cn.md)。
每个字段以 unsigned LEB128 key 开始：

```text
key = (field_number << 3) | wire_type
```

field number 为 1…65535。wire type：0=varint，1=8-byte big-endian fixed64，
2=LEB128 length 加对应字节，5=4-byte big-endian fixed32；3、4、6、7 非法。
长度必须适配剩余 payload 和 `size_t`。malformed、truncated、overlong、overflow varint 都是
decode error；窄整数必须适配 C 类型，否则返回 `WL_CODEC_ERR_OVERFLOW` 而不截断。

```text
zigzag32(v) = (uint32_t(v) << 1) ^ uint32_t(v >> 31)
zigzag64(v) = (uint64_t(v) << 1) ^ uint64_t(v >> 63)
```

实现必须避免 signed-overflow UB。encoder 按 field number 升序输出。decoder 接受任意
顺序，但拒绝重复 optional/packed、已知字段 wire type 不匹配。repeated 每个元素使用
完整 tag/body。packed 只有一次 length-delimited occurrence，body 必须恰为
`count * 4/8`，无逐元素 tag；其他长度全部拒绝。

允许的未知 field 会被跳过且不保留。缺失 optional/optional-packed 的 `has_` 为 false，
值取 default 或零/空；显式出现且等于 default 仍会编码并保持 `has_=true`。required 使用
相同 wire/C 表示，但 encode/decode 缠失时返回
`WL_CODEC_ERR_MISSING_REQUIRED_FIELD`。畸形字段仍报告 malformed，不改报 missing。

## 3. 生成 C API

生成代码只依赖小型 `wirelink/codec.h`。每个 message 生成 `*_clear()`、
`*_encoded_size()`、`*_encode()`、`*_decode()` 和 `*_MESSAGE_ID`。缺 required 时
encoded size 为 `SIZE_MAX`。

若 payload 有静态上限，生成 `*_HAS_MAX_ENCODED_SIZE=1` 和
`*_MAX_ENCODED_SIZE`。fixed scalar、enum、packed、有界 string/bytes 和传递有界的
nested message 都可参与计算；无界 string/bytes 或无 schema count 的普通 repeated 会
使上限不可用。上限包含 key 和所有 length prefix。

API 无动态分配。解码的 bytes/string 借用 `input`，不以 NUL 结尾；来自
`wl_event_t.payload` 的 view 必须在 `wl_event_release()` 前消费。

repeated C field 包含调用方 array pointer、count、capacity；`*_clear()` 清 presence/count
但保留 pointer/capacity。输入超出存储时返回 `WL_CODEC_ERR_CAPACITY`，不分配。decode
错误后输出未指定，应 clear 或重新 decode。packed 则生成 inline fixed array 和 presence
flag，无 runtime count/capacity/pointer。plain packed 可选，required packed 强制存在。
例如 30 个 float32 的 field 由 1-byte key、1-byte length 和 120-byte body 组成，共
122 bytes。

WLC 始终生成 codec，可额外生成 typed send/dispatch 和 profile-driven LATEST/FIFO/RPC
runtime。它们是独立 translation unit，因此 codec-only target 不会引入 core/runtime。

## 4. 示例与语义

```wl
version 1;

enum MotorMode @id(1) {
  MOTOR_MODE_DISABLED = 0;
  MOTOR_MODE_CURRENT = 1;
  MOTOR_MODE_POSITION = 2;
}

message MotorCommand @id(16) {
  required uint32 operation_id @id(1);
  required MotorMode mode @id(2);
  optional fixed32 target_milliamps @id(3);
  optional bytes<64> vendor_extension @id(4);
}

message SensorSample @id(17) {
  optional uint32 sensor_id @id(1);
  optional int32 value_milliunits @id(2);
}

message TelemetryBatch @id(18) {
  repeated SensorSample samples @id(1);
  optional string<31> source @id(2) [default = "board"];
  optional uint64 timestamp_us @id(3);
}

message ArmMitCommand @id(19) {
  required packed float32 controls[30] @id(1);
  required uint16 sequence @id(2);
  required float32 dt_s @id(3);
}
```

`MotorCommand = 16` 对应 `message_id=16`；是否可靠由
`wl_send_reliable()` 或 profile 决定。`TelemetryBatch.samples` decode 前需要应用提供
静态 array，`source` 借用 event payload。`ArmMitCommand.controls` 内联存储，出现时
始终含 30 个 binary32 值。

## 5. Generator 与发布 Gate

release gate 覆盖所有 built-in、default 范围与 UTF-8、enum `int32_t`、有界无环嵌套、
canonical field order、scalar boundary、未知字段跳过、duplicate rejection、repeated
capacity、malformed key/length 和兼容演进。

dense numeric 还要求 float32/64 bit-exact golden vector（正负零、infinity、非 canonical
NaN）、生成 C11 编译执行、packed length/duplicate/wire-type/truncation 拒绝、type/count/
cardinality 兼容检查，以及 30-element binary32 vector 始终为 122 bytes。required、窄
整数和 bounded borrowed field 同样覆盖 encode/decode 缺失、overflow 不截断、UTF-8
byte bound、pointer provenance、identity/manifest 与静态 maximum。
