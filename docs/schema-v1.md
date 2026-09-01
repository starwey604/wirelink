# WLC schema and payload format v1

Status: **frozen pre-1.0 baseline, including required and dense numeric
fields**.

This document defines the application-payload format carried by a Wirelink
`DATA` packet. It is deliberately separate from the Wirelink packet header,
COBS envelope, integrity trailer, and link-level ACK. A generated codec never
implements retransmission or treats a link ACK as an application result.

## 1. Schema source

The existing `.wl` grammar is retained:

```text
schema         = "version" positive-integer ";" item+
item           = declaration | reservation
declaration    = message | enum
reservation    = "reserved" positive-integer ";"
message        = "message" identifier "=" positive-integer
                 "{" (field | reservation)* "}"
field          = optional-field | required-field | repeated-field
                 | packed-field | required-packed-field
optional-field = "optional" type identifier "=" positive-integer
                 ("[" "default" "=" literal "]")? ";"
required-field = "required" type identifier "=" positive-integer ";"
repeated-field = "repeated" type identifier "=" positive-integer ";"
packed-field   = "packed" packed-type identifier "[" positive-integer "]"
                 "=" positive-integer ";"
required-packed-field = "required" "packed" packed-type identifier
                        "[" positive-integer "]" "=" positive-integer ";"
packed-type    = "float32" | "float64" | "fixed32" | "fixed64"
enum           = "enum" identifier "=" positive-integer "{" enum-item* "}"
```

There are no implicit IDs, maps, `oneof`, services, extensions, or
schema-level reliability annotations in v1. Reliability is selected by the
binding profile or the application send API. A packed field is a fixed-count
numeric array, not a general repeated-field encoding. A required field cannot
declare a default or be repeated; fixed-count required vectors use
`required packed`.

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
change compatibly. A packed array's exact element count is part of its
cardinality and wire identity. Defaults are local decode behaviour and are not
a wire change. Adding a required field to an existing message is incompatible,
as is changing integer width or signedness even when both forms use wire type
zero.

### 1.1 Built-in types

The generator's v1 built-in set is:

| Type | C representation | Wire form |
| --- | --- | --- |
| `bool` | `bool` | unsigned varint, only `0` or `1` |
| `uint8`, `uint16`, `uint32`, `uint64` | exact-width unsigned C integer | unsigned LEB128 varint |
| `int8`, `int16`, `int32`, `int64` | exact-width signed C integer | ZigZag, then unsigned LEB128 |
| `fixed32`, `fixed64` | `uint32_t`, `uint64_t` | 4/8 bytes, big-endian |
| `float32`, `float64` | `float`, `double` | IEEE-754 binary32/binary64 bits as 4/8 bytes, big-endian |
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

Generated headers statically require the target C implementation to use
4-byte IEEE binary32 `float` and 8-byte IEEE binary64 `double` when the
corresponding type appears. Encoding and decoding move their object bits with
`memcpy`; there is no numeric conversion or aliasing cast. Signed zero,
infinities, and NaN payload bits therefore round-trip exactly.

`bytes` has no default. A `string` default is a UTF-8 source literal. Integer,
boolean, fixed-width integer, and enum defaults must fit their declared type;
an enum default must name an existing numeric enum value. Explicit float
defaults are not accepted until the grammar defines a canonical,
locale-independent float literal; an absent float clears to positive zero.
Repeated and packed fields never have defaults.

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

`bool`, all variable integers, and enums use type 0; `fixed64` and `float64`
use type 1; `bytes`, `string`, nested messages, and packed arrays use type 2;
and `fixed32` and `float32` use type 5. Length values must fit the remaining
payload and `size_t`; malformed, truncated, overlong, or overflowing varints
are decode errors. A decoded `uint8`, `uint16`, `int8`, or `int16` value must
fit its declared C type; otherwise decode returns `WL_CODEC_ERR_OVERFLOW`
without modifying the field through a narrowing conversion. Wire types 3, 4,
and 6--7 are invalid.

Signed values use the exact mappings below before unsigned LEB128 encoding:

```text
zigzag32(v) = (uint32_t(v) << 1) ^ uint32_t(v >> 31)
zigzag64(v) = (uint64_t(v) << 1) ^ uint64_t(v >> 63)
```

Implementations must express the shifts without signed-overflow undefined
behaviour. Fixed-width values are network byte order to match Wirelink's packet
header convention.

The encoder emits fields in ascending field-number order. A decoder accepts
known fields in any order but rejects a duplicate `optional` or `packed` field
and a known field whose wire type does not match its declaration. A `repeated`
field is represented by one complete tag/body pair per element.

A packed field is one optional length-delimited occurrence. Its body is
exactly `count * 4` or `count * 8` bytes, containing fixed-width elements in
array order and network byte order, without per-element tags. A decoder rejects
any other body length, including a length divisible by the element width but
different from the declared count. This exact shape is what allows generated
C to use bounded inline storage.

Unknown fields using a permitted wire type are skipped and discarded. This is
the forward-compatibility mechanism; unknown data is not retained if an
application decodes and re-encodes a message.

An absent optional or optional-packed field leaves `has_<field>` false and its
value initialized to the schema default (or the type's zero/empty value). A
present field equal to its default is still emitted and leaves `has_<field>`
true. Required scalar, nested, and packed fields use the same wire form and
retain the same `has_<field>` representation in C, but encode and decode return
`WL_CODEC_ERR_MISSING_REQUIRED_FIELD` unless every required field is present.
Malformed or truncated field bytes remain `WL_CODEC_ERR_MALFORMED`; they are
not reclassified as a missing field.

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

typedef int32_t wl_codec_status_t;
enum {
  WL_CODEC_OK = 0,
  WL_CODEC_ERR_MALFORMED,
  WL_CODEC_ERR_OVERFLOW,
  WL_CODEC_ERR_CAPACITY,
  WL_CODEC_ERR_WIRE_TYPE,
  WL_CODEC_ERR_DUPLICATE_FIELD,
  WL_CODEC_ERR_UTF8,
  WL_CODEC_ERR_INVALID_VALUE,
  WL_CODEC_ERR_MISSING_REQUIRED_FIELD,
};
```

`WL_CODEC_ERR_MISSING_REQUIRED_FIELD` reports schema cardinality validation.
It is distinct from malformed wire bytes and from a present field whose value
is outside its permitted domain. `*_encoded_size()` returns `SIZE_MAX` for an
object with a missing required field.

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

A packed field instead generates an inline fixed array and a presence flag:

```c
bool has_controls;
float controls[30];
```

It has no runtime count, capacity, pointer, or allocation failure. `*_clear()`
sets the presence flag false and clears the complete array to zero. Plain
`packed` is optional; `required packed` has the same storage and wire layout
but enforces presence. When present, `packed float32 controls[30] = 1;`
occupies 122 payload bytes: the one-byte field key, the one-byte length `120`,
and 120 element bytes. This dense representation is intended for fixed-shape
control vectors, matrices, and similar numeric payloads where an ordinary
repeated field's per-element tag is unnecessary.

WLC always emits the codec pair and may additionally emit typed send/dispatch
bindings and a profile-driven LATEST/FIFO/RPC runtime. Generated artifacts are
separate translation units, so a codec-only target does not pull Wirelink core
or application-runtime symbols into its link.

## 4. Example

```wl
version 1;

enum MotorMode = 1 {
  MOTOR_MODE_DISABLED = 0;
  MOTOR_MODE_CURRENT = 1;
  MOTOR_MODE_POSITION = 2;
}

message MotorCommand = 16 {
  required uint32 operation_id = 1;
  required MotorMode mode = 2;
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

message ArmMitCommand = 19 {
  required packed float32 controls[30] = 1;
  required uint16 sequence = 2;
  required float32 dt_s = 3;
}
```

`MotorCommand` maps to Wirelink `message_id` 16. A reliable command is an
application choice (`wl_send_reliable()`); it is not represented in the
schema. `TelemetryBatch.samples` requires an application-provided static array
of `sensor_sample_t` before decode, while `source` borrows the event payload.
`ArmMitCommand.controls` is stored inline and, when present, always carries
exactly thirty binary32 values.

## 5. Required generator and release gates

WLC semantic analysis and generated-C tests are release gates for the built-in
types, default ranges and UTF-8, enum `int32_t` range, and bounded acyclic
message nesting. Golden tests cover canonical field order, scalar boundaries,
empty and maximum-length length-delimited fields, unknown-field skipping,
optional duplicate rejection, repeated-capacity failure, nested messages,
malformed keys and lengths, UTF-8 rejection, and schema evolution with
reserved IDs. Cross-version tests prove that an older decoder skips an added
field and that a newer decoder accepts an absent older field.

Dense numeric coverage additionally requires bit-exact `float32` and
`float64` golden vectors for positive and negative zero, infinities, and a NaN
with a non-canonical payload; generated C11 compilation and execution; packed
length, duplicate, wire-type, and truncation rejection; packed compatibility
checks for type, count, and cardinality; and a checked 30-element binary32
control vector whose encoded field size remains 122 bytes. Required-field
coverage includes missing scalar, nested, and packed values on both encode and
decode. Narrow-integer coverage includes min/max golden bytes, default-range
validation, exact-width C/C++ storage, and decode overflow rejection without
truncation.
