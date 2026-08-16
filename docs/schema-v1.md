# WLC schema and payload format v1

Status: **frozen for the first WLC code generator**.

This document defines the application-payload format carried by a Wirelink
`DATA` packet. It is deliberately separate from the Wirelink packet header,
COBS envelope, integrity trailer, and link-level ACK. A generated codec never
implements retransmission or treats a link ACK as an application result.

## 1. Schema source

The existing `.wl` grammar is retained:

```text
schema      = "version" positive-integer ";" item+
item        = declaration | reservation
declaration = message | enum
reservation = "reserved" positive-integer ";"
message     = "message" identifier "=" positive-integer "{" (field | reservation)* "}"
field       = ("optional" | "repeated") type identifier "="
              positive-integer ("[" "default" "=" literal "]")? ";"
enum        = "enum" identifier "=" positive-integer "{" enum-item* "}"
```

There are no implicit IDs, `required` fields, maps, `oneof`, services, packed
fields, extensions, or schema-level reliability annotations in v1. Reliability
is selected when the application calls `wl_send_reliable()` or
`wl_send_unreliable()`.

`version` is the schema revision used by WLC compatibility checking. It is not
the Wirelink packet version and is not serialized in a payload. A first schema
uses `version 1`; a compatible edit increments its revision and is checked
against the preceding semantic model.

A message's nonzero 16-bit declaration ID is the `message_id` passed to the
Wirelink core and carried in the DATA header. Enum declaration IDs remain
stable compiler symbols under the current front-end's global-ID rule, but are
not packet message IDs. The generated header exposes a `*_MESSAGE_ID` constant
for messages only.

Fields retain the current nonzero 16-bit, message-local number allocation.
Removed messages, enums, fields, and enum values remain reserved forever. A
message name, declaration ID, field number, field type, or cardinality cannot
change compatibly. Defaults are local decode behaviour and are not a wire
change.

### 1.1 Built-in types

The generator's v1 built-in set is:

| Type | C representation | Wire form |
| --- | --- | --- |
| `bool` | `bool` | unsigned varint, only `0` or `1` |
| `uint32`, `uint64` | `uint32_t`, `uint64_t` | unsigned LEB128 varint |
| `int32`, `int64` | `int32_t`, `int64_t` | ZigZag, then unsigned LEB128 |
| `fixed32`, `fixed64` | `uint32_t`, `uint64_t` | 4/8 bytes, big-endian |
| `bytes` | `wl_codec_bytes_t` | length-delimited bytes |
| `string` | `wl_codec_string_t` | length-delimited, valid UTF-8 |

An enum is encoded exactly as an `int32` ZigZag varint. Generated enum types
are integer typedefs plus named constants, rather than a restrictive C `enum`,
so a newer sender's unknown enum value remains representable by an older
receiver. V1 enum declarations may use values in the `int32_t` range.

A field may also name another message. That field is length-delimited and its
contents are a complete nested v1 message body. Direct or indirect recursive
message graphs are rejected. The compiler also rejects a nested-message depth
greater than eight; this makes generated recursive decode stack use bounded and
reviewable.

`bytes` has no default. A `string` default is a UTF-8 source literal. Numeric,
boolean, and enum defaults must fit their declared type; an enum default must
name an existing numeric enum value. Repeated fields never have defaults.

## 2. Wire format

A payload is a sequence of zero or more tagged fields. There is no payload
header and no schema revision field: the Wirelink DATA `message_id` chooses the
generated decoder.

Each field begins with an unsigned-LEB128 key:

```text
key = (field_number << 3) | wire_type
```

`field_number` must be 1 through 65535. The permitted wire types are:

| Value | Name | Body |
| ---: | --- | --- |
| 0 | varint | unsigned LEB128 value |
| 1 | fixed64 | exactly 8 big-endian bytes |
| 2 | length-delimited | unsigned-LEB128 length followed by that many bytes |
| 5 | fixed32 | exactly 4 big-endian bytes |

`bool`, all variable integers, and enums use type 0; `fixed64` uses type 1;
`bytes`, `string`, and nested messages use type 2; and `fixed32` uses type 5.
Length values must fit the remaining payload and `size_t`; malformed, truncated,
overlong, or overflowing varints are decode errors. Wire types 3, 4, and 6--7
are invalid.

Signed values use the exact mappings below before unsigned LEB128 encoding:

```text
zigzag32(v) = (uint32_t(v) << 1) ^ uint32_t(v >> 31)
zigzag64(v) = (uint64_t(v) << 1) ^ uint64_t(v >> 63)
```

Implementations must express the shifts without signed-overflow undefined
behaviour. Fixed-width values are network byte order to match Wirelink's packet
header convention.

The encoder emits fields in ascending field-number order. A decoder accepts
known fields in any order but rejects a duplicate `optional` field and a known
field whose wire type does not match its declaration. A `repeated` field is
represented by one complete tag/body pair per element; packed repeated numeric
encoding is not part of v1. Unknown fields using a permitted wire type are
skipped and discarded. This is the forward-compatibility mechanism; unknown
data is not retained if an application decodes and re-encodes a message.

An absent optional field leaves `has_<field>` false and its value initialized to
the schema default (or the type's zero/empty value). A present field equal to
its default is still emitted and leaves `has_<field>` true. This preserves
presence without requiring a separate on-wire marker.

## 3. Generated C contract

Generated code depends only on a small future `wirelink/codec.h` runtime. Its
v1 public primitives are conceptually:

```c
typedef struct {
  const uint8_t *data;
  size_t length;
} wl_codec_bytes_t;

typedef struct {
  const char *data;
  size_t length;
} wl_codec_string_t;

typedef enum {
  WL_CODEC_OK = 0,
  WL_CODEC_ERR_MALFORMED,
  WL_CODEC_ERR_OVERFLOW,
  WL_CODEC_ERR_CAPACITY,
  WL_CODEC_ERR_WIRE_TYPE,
  WL_CODEC_ERR_DUPLICATE_FIELD,
  WL_CODEC_ERR_UTF8,
  WL_CODEC_ERR_INVALID_VALUE,
} wl_codec_status_t;
```

For every message `MotorStatus`, WLC generates:

```c
#define MOTOR_STATUS_MESSAGE_ID 17U

void motor_status_clear(motor_status_t *value);
size_t motor_status_encoded_size(const motor_status_t *value);
wl_codec_status_t motor_status_encode(const motor_status_t *value,
                                      uint8_t *out, size_t out_capacity,
                                      size_t *out_length);
wl_codec_status_t motor_status_decode(const uint8_t *input, size_t input_length,
                                      motor_status_t *out);
```

The API is allocation-free. `bytes` and `string` decode to borrowed views into
`input`; they are not NUL-terminated and are valid only while that input is
valid. In particular, a decoded view derived from `wl_event_t.payload` must be
consumed before `wl_event_release()`. This aligns generated decode with the
core's RX borrowing model.

Each repeated C field contains a caller-provided array pointer, count, and
capacity. `*_clear()` resets presence and counts while preserving those array
pointers and capacities, so an application can initialize static storage once.
For example, a repeated `uint32 samples` generates the equivalent of:

```c
uint32_t *samples;
size_t samples_count;
size_t samples_capacity;
```

Repeated nested messages use the same pattern with an array of generated
message structs. A decoder returns `WL_CODEC_ERR_CAPACITY` rather than
allocating when an input contains more values than supplied storage. On any
decode error the output is unspecified and must be cleared or decoded again
before use. Encoding validates all pointer/length/count combinations and never
retains an input pointer after it returns.

The first generator emits codec functions only. It does not emit convenience
`wl_send_*` wrappers, RPC/request-response bindings, or response correlation;
an application encodes into its own payload buffer and passes that buffer to
the Wirelink core. The core copies accepted TX payload bytes, so the encode
scratch buffer may be reused immediately after a successful send call.

## 4. Example

```wl
version 1;

enum MotorMode = 1 {
  MOTOR_MODE_DISABLED = 0;
  MOTOR_MODE_CURRENT = 1;
  MOTOR_MODE_POSITION = 2;
}

message MotorCommand = 16 {
  optional uint32 operation_id = 1;
  optional MotorMode mode = 2 [default = 0];
  optional fixed32 target_milliamps = 3;
  optional bytes vendor_extension = 4;
}

message SensorSample = 17 {
  optional uint32 sensor_id = 1;
  optional int32 value_milliunits = 2;
}

message TelemetryBatch = 18 {
  repeated SensorSample samples = 1;
  optional string source = 2 [default = "board"];
  optional uint64 timestamp_us = 3;
}
```

`MotorCommand` maps to Wirelink `message_id` 16. A reliable command is an
application choice (`wl_send_reliable()`); it is not represented in the
schema. `TelemetryBatch.samples` requires an application-provided static array
of `sensor_sample_t` before decode, while `source` borrows the event payload.

## 5. Required generator gates

Before generated output is accepted as v1, WLC must add semantic checks for the
new built-ins, default ranges/UTF-8, enum `int32_t` range, and bounded acyclic
message nesting. Golden tests must cover canonical field order, all scalar
boundaries, empty and maximum-length length-delimited fields, unknown-field
skipping, optional duplicate rejection, repeated-capacity failure, nested
messages, malformed keys/lengths, UTF-8 rejection, and schema evolution with
reserved IDs. Cross-version tests must prove that an older decoder skips an
added field and that a newer decoder accepts an absent older field.
