# WLC codec conformance fixtures

`previous/control.wl` and `current/control.wl` are a compatible schema pair,
compiled with WLC 0.14. The checked-in C codecs and manifests test schema
payload evolution under the current little-endian codec contract, not 0.12
binary interoperability. Optional packed arrays explicitly use `optional packed`.

Regenerate each revision with the matching compiler:

```sh
wlc compile tests/fixtures/wlc/current/control.wl --out-dir tests/fixtures/wlc/generated/current
wlc compile tests/fixtures/wlc/previous/control.wl --out-dir tests/fixtures/wlc/generated/previous
```

Tests preserve IEEE bits, packed lengths, unknown fields, optional defaults and
the 4096-byte Chunk schema bound. Bounded strings/bytes are inline; unbounded
strings/bytes remain borrowed views. Node routes require finite packet bounds.
Current typed execution fixtures live in `tests/fixtures/static_endpoint`, with
explicit external I/O and dynamic handlers to exercise asynchronous ownership.
