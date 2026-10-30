# positional-v1

`positional-v1` is an explicit, schema-dependent business-body format. WLC selects it with `codec = positional_v1` on an ordinary message route or a managed RPC method. The defaults remain `schema_v1` for ordinary routes and `managed_tagged` for managed RPC. RPC `guarantee` is independent and defaults to `replay`. Mapped RPC metadata only supports `schema_v1`.

An encoded body contains the optional-field bitmap followed by present field values in ascending numeric field-ID order. Field names, field tags, message IDs, C layout/padding, and a format discriminator are absent from the body. The enclosing Wirelink message still supplies its message ID, integrity and delivery policy. Managed RPC still supplies its v4 session/operation/rejection metadata before the business body.

## Presence and order

Only optional fields consume bitmap bits. Number these fields in ascending field-ID order, starting at bit 0. Within each bitmap byte, lower numbered bits come first. The bitmap length is fixed at `ceil(optional_field_count / 8)` bytes, including when every optional field is absent. Unused high bits in its final byte must be zero.

Required fields consume no bitmap bits and must always be encoded. A decoder sets `has_*` on every successfully decoded required or present optional field. Absent optional fields have `has_* = false` and retain their schema defaults. A schema with no fields has an empty body; a schema with only absent optional fields still has its bitmap.

For example, given required `uint32 a @id(1)`, optional `bool b @id(2)` and required `fixed32 c @id(3)`, the value `a=1, b=absent, c=128` is `00 01 80 01`. With `b=true`, it is `01 01 01 80 01`. The leading byte is the bitmap, not a tag.

## Values

| Schema type | Encoding |
| --- | --- |
| bool | One byte, exactly 0 or 1 |
| uint8/16/32/64 | Minimal unsigned base-128 varint; reject values beyond the declared domain |
| int8/16/32/64 | ZigZag to the corresponding unsigned domain, then minimal varint |
| enum | Open signed int32 domain, ZigZag and minimal varint; undeclared int32 values are legal |
| scalar fixed32 | Full-domain uint32 minimal varint, as in managed tagged bodies |
| scalar fixed64 | Eight bytes, most significant byte first |
| float32/64 | Four/eight bytes of the IEEE-754 binary32/binary64 representation, most significant byte first |
| packed fixed32/64 or float32/64 | Minimal varint payload length, then exactly the schema's fixed element count in big-endian fixed-width representation |

ZigZag maps nonnegative `n` to `2*n` and negative `n` to `-2*n-1`, across the full declared signed domain. The 64-bit minimum therefore encodes as unsigned `UINT64_MAX`. A uint64 varint has at most ten bytes, and its tenth byte is at most 1. Nonminimal encodings such as `80 00` are invalid.

Packed fixed32 uses four bytes per element even though scalar fixed32 uses varint. The packed payload length must equal `count * element_width`; the length is not an element count. Optional packed arrays use one bitmap bit for the whole array. Required packed arrays always appear. Floats preserve raw NaN payloads, infinities and both signs of zero; decoding does not normalize or evaluate the floating-point value.

Nested messages, repeated fields, bytes and strings are unsupported in this revision. WLC rejects a positional route/method containing any of them before writing artifacts. Other messages in the same schema can use tagged codecs.

Codec selection applies to the top-level route or RPC body. A flat `Child` can
use positional-v1 independently while a Schema-v1 `Parent` embeds that same
business type. Inside `Parent`, `Child` retains Schema-v1 tags and scalar
encodings; inside a managed tagged body it retains managed tagged encodings.
WLC emits separate internal descriptors where needed. Message ID ordering,
route ordering and RPC service names do not change nested bytes or canonical
replay fingerprints. Standalone route/method bounds use their own selected
codec, while enclosing bounds include the enclosing format's nested encoding.

## Rejection and ownership

Reject a truncated bitmap/value/array, nonzero unused bitmap bits, nonminimal or overflowing varint, bool outside 0/1, wrong packed length, and any trailing bytes. There is no unknown-field skipping, format sniffing, or fallback to tagged. Public owned-value decoding is transactional: failure leaves the caller's output unchanged. Private RPC decoding writes directly to unpublished typed result fields; only a successful decode can commit the result as valid. View decoding can modify scratch storage on failure, which must not be published.

## Identity and compatibility

The external format identity is `positional-v1`. Schema identity covers field IDs, cardinalities, types and defaults; binding-profile identity covers each route/method's codec and RPC guarantee. Identify a deployment with both identities. Local owned-storage layout is not a wire identity input. A change to the positional rules requires a new format name; changing schema layout requires a different schema identity.

Codegen/core contract 0.12 introduces executable positional codecs. Generated codec sources check the matching header's message codec, runtime sources check the matching header's schema/profile identities, and runtimes check ordinary route codecs. Mixing 0.11 generated runtimes with 0.12 core is rejected. `wlc plan` reports full-domain maximum body sizes, presence bits, and method capacities. Generated manifests record format symbols and artifact digests.

Format identity is not an additional on-wire negotiation packet. Independently deployed peers must compare/validate the schema/profile pair before exchanging positional traffic, or use separately assigned routes agreed by the application. A raw body can coincidentally be valid under two schemas; successful body decoding cannot prove profile agreement. The benchmark verifies the generated encoders, both manifests and peer build/profile identity before accepting measurements.

Tag-free encoding changes evolution guarantees: adding an optional field can enlarge the fixed bitmap or introduce a trailing value; adding a required field changes every body. Deploy matching schemas or introduce a separately identified format/route. Tagged codecs retain their existing field-order/unknown-field behavior. Replay still compares canonical decoded business values using the existing canonical-request contract, independent of the received body representation; correlate remains a separately declared guarantee without duplicate suppression/history.

## Vectors and generation

`wlc/tests/fixtures/positional.c` contains independent byte vectors, integer-domain errors, float-bit checks, bitmap/array/truncation/trailing checks, transactional owned failures and actual endpoint calls. `wlc/tests/positional.rs` runs both body codecs with replay/correlate and reliable/unreliable delivery under strict C11, checks deterministic output, and rejects mismatched generated headers. Frozen Schema-v1 and managed tagged conformance vectors remain separate regression tests.

The owned/view and C++ conversion fixtures include signaling NaNs under an
additional GCC x87 run (`WLC_TEST_X87=1`); byte copies avoid quieting them during
conversion. P4 hardware results and the separate replay/guarantee/Sample
comparisons are recorded in [the P4 report](codec-rpc-refactor-round-2-p4-result-cn.md).
