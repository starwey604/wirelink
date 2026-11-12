# Frozen WLC codec conformance fixtures

`previous/control.wl` and `current/control.wl` are a compatible schema pair.
Their checked-in `control.c`, `control.h` and `control_values.h` files are frozen
0.12 pure codec artifacts. Keep their bytes and the business schemas unchanged:
they independently test payload evolution, IEEE bits, packed lengths, strings,
unknown fields and the 4096-byte Chunk schema bound. The standalone codec
contract explicitly accepts this frozen 0.12 fixture; this does not retain any
old endpoint or execution ABI.

The two `control_manifest.json` files are unchanged **historical provenance**
for the original complete artifact sets, not manifests for the current retained
subset. Do not run current artifact-set validation on those historical files.
The v1 binding profiles next to the schemas are historical/rejection inputs;
they no longer describe supported execution or a regeneration command.

Generate current pure codecs into a fresh directory with matching WLC 0.13:

```sh
wlc compile tests/fixtures/wlc/current/control.wl --out-dir /tmp/control-codec-013
```

Do not overwrite frozen fixtures with that output. New typed execution fixtures
live in `tests/fixtures/static_kernel` and select an explicit v2 profile. Current
ordinary routes, RPC ownership, old-instance isolation and application jobs run
in the kernel tests, session tests and migrated tutorials. FIFO/LATEST remain
independent containers; applications own any scheduling or retention policy.
The USB object sample uses the complete public schema through a pure codec,
with an explicit deployment chunk limit; there is no framework Bulk service,
replay cache, reliable FIFO execution or deferred reply token.
