# Positional-v1 ordinary-message codec

[Packet and codec selection](packet-protocol.md) · [Profiles](link-profiles.md)

Select `codec = positional_v1` on an ordinary send/direct route when both peers
use the exact same field layout. This schema-dependent business-body format
places an optional-field bitmap before values in ascending numeric field-ID
order. Field names/tags, message IDs, C padding and a codec discriminator are
absent. The enclosing DATA packet supplies the message ID.

Profile v2 supports this codec for ordinary messages. RPC requires
`managed_tagged`; positional RPC and mapped RPC are rejected. Connector choice
is independent of the payload codec.

## Presence and order

Only optional fields consume bitmap bits. Number them in ascending field-ID
order from bit `0`, least-significant bit first within each byte. The fixed
bitmap length is `ceil(optional_field_count / 8)`, even when every optional
field is absent. Unused high bits in the last byte must be zero.

Required fields consume no bitmap bits and must always be present. Successful
decode sets `has_*` for required/present optional fields. Absent optional fields
retain defaults with false presence. An empty schema has an empty body; a
schema containing only absent optional fields still has its bitmap.

For required `uint32 a @id(1)`, optional `bool b @id(2)` and required
`fixed32 c @id(3)`, `a=1, b=absent, c=128` encodes as `00 01 80 01`.
With `b=true`, it is `01 01 01 80 01`. The leading byte is the bitmap, not a tag.

## Values

| Schema type | Encoding |
| --- | --- |
| `bool` | One byte, exactly `0` or `1` |
| `uint8/16/32/64` | Minimal unsigned base-128 varint within the full declared domain |
| `int8/16/32/64` | Full-domain ZigZag, then minimal unsigned varint |
| enum | Open signed `int32_t` domain, ZigZag/varint, including undeclared values |
| Scalar `fixed32` | Full-domain `uint32_t` minimal varint |
| Scalar `fixed64` | Eight bytes, big-endian |
| `float32/64` | IEEE binary32/binary64 bits, four/eight bytes big-endian |
| Packed `fixed32/64` or `float32/64` | Minimal varint byte length, then the exact fixed element count in big-endian fixed-width representation |

ZigZag maps nonnegative `n` to `2*n` and negative `n` to `-2*n-1` across the
full signed domain; `INT64_MIN` maps to unsigned `UINT64_MAX`. A `uint64`
varint uses at most ten bytes, with final byte at most `1`. Nonminimal values
such as `80 00` are invalid. Packed `fixed32` uses four bytes per element,
although scalar `fixed32` is varint. Packed byte length must equal
`count * element_width`, not the element count. An optional packed array uses
one presence bit for the whole array. Floats preserve NaN payloads, infinities
and both zero signs without numerical normalization.

Nested messages, repeated fields, bytes and strings are unsupported; WLC rejects
a positional route containing them before writing artifacts. Other routes may
use tagged codecs. A flat `Child` can use positional independently while a
Schema-v1 `Parent` embeds it: inside the parent it uses Schema-v1 tags/scalar
encodings. Inside managed tagged bodies, nested types use managed tagged
encoding. Selected route bounds and enclosing nested bounds follow their own
formats, not the child's independent ordinary route.

## Validation and ownership

Reject truncated bitmap/value/array, nonzero unused bitmap bits, nonminimal or
overflowing varints, invalid bool, wrong packed length and trailing bytes.
There is no unknown-field skipping, format sniffing or tagged fallback. Public
owned decode is transactional: failure leaves caller output unchanged. View
decode may modify scratch on error; do not publish failed output. Kernel
callbacks borrow decoded views until return; copy results needed later.

## Identity and compatibility

The wire identity is `positional-v1`. Schema identity includes field IDs,
cardinalities, types and defaults; profile identity includes route codec and
deployment choices. Local owned-storage layout is not wire identity. A change
to format rules requires a new format identity; changed field layout needs a
compatible deployment decision.

There is no additional negotiation packet. Validate the schema/profile pair
before exchanging positional traffic, or assign separately agreed routes.
A body can coincidentally decode under two layouts; successful decoding does
not prove peer agreement. Adding optional fields can change bitmap width or
introduce trailing values; adding required fields changes every body. Deploy
matching layouts or a separately identified route. Use tagged codecs when
unknown-field skipping is needed.

## Vectors and generation checks

In the matching WLC repository, `tests/fixtures/positional.c` and
`tests/positional.rs` check independent bytes, scalar boundaries, floats,
bitmap/packed/truncation/trailing errors, transactional owned failures and
rejection of unsupported shapes/mismatched generated headers. Generated C
compiles under strict C11. Public Schema-v1 and managed tagged RPC have separate
vectors. Owned/view/C++ conversion fixtures can also check signaling NaNs with
`WLC_TEST_X87=1`; bit copies avoid floating-point conversion.

Run the relevant compiler checks using the setup in
[development](development.md#build-matching-wlc-and-generated-tutorials).
