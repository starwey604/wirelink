# API subtraction: implementation and validation

Date: 2026-09-06. Internal codegen ABI 20; no main merge, release tag, or board run.
[中文](api-simplification-cn.md).

## Implemented boundaries

- `@id(n)` makes declaration/field numbering explicit. Legacy `= n` remains
  equivalent in semantic identities, manifests, generated C and encoded bytes.
  Enum values and optional defaults continue to use `=`.
- Managed RPC business messages contain only arguments/results. Runtime metadata,
  typed call handles/results, and reply tokens replace application-owned numbering.
  Business rejection needs no fabricated response body. Default call/reply APIs
  return RPC error codes directly; tagged diagnostic details remain optional.
- Handles validate endpoint, incarnation, service and slot generation. Subsequent
  core handle inspection/cancel/release is O(1). Unmatched/terminal responses are
  diagnostic without failing another call; ID reuse has the limitations below.
- Managed requests encode into TX claims and replies into reserved cache storage;
  the former typed metadata-injection scratch is removed. All-three-field mappings
  preserve the existing format; migrating modes requires upgrading both peers.
- English/Chinese tutorials now introduce business calls first; wire metadata,
  mappings, session identity and replay limits live in integration/contracts.

## Costs

Managed RPC adds a fixed 12-byte prefix, independently of business codecs and
Compact-v1 framing. The tutorial's first `20 + 22` request/response payloads change
from mapped 6/6 bytes to managed 16/14 bytes, excluding outer framing. This is a
boundary/sizing simplification, not a compressed-wire optimization.

Local GCC 16.2.1, CMake Release versus the previously built ABI 19 tutorial:

| Item | Before | Current |
| --- | ---: | ---: |
| Temperature endpoint, x86-64 static size | 1424 B | 1424 B |
| Addition endpoint, x86-64 static size | 2384 B | 2384 B |
| Addition executable, GNU size `text` | 90704 B | 90648 B |
| Generated RPC runtime object, Cortex-M7/Thumb/`-Os`, `text` | 6177 B | 6185 B |

The new example also handles overflow rejection. These are integration size checks,
not isolated attribution or evidence of lower latency/CPU time. H7 timing remains
deferred until hardware is available.

## Verification

WLC Rust/generated-C tests, rustfmt and full-target/feature Clippy; syntax artifact
equivalence; all four RPC delivery combinations; shared codec/named runtime and
mixed managed/mapped generation; C11/C++20 compilation. Real-core simulation covers
concurrent reversed replies, timeout/cancellation, late/malformed replies, rejection,
replay/conflict, stale handles/tokens and failed-encode cleanup. ASan/UBSan and the
existing Cortex-M size gates pass. Zephyr `unit_testing`/`native_sim`: 28 scenarios,
202 cases pass. Normal and WLC installed-package consumers pass. The control
conformance schemas/codec bytes remain unchanged; only ABI metadata is regenerated.

## Further review, not silently promised

1. Response freshness across client reconstruction: local generations do not add
   wire authority. Reusing a 32-bit ID cannot rule out arbitrarily stale replies.
   Evaluate echoing client session identity or a connection-reset protocol;
   either must be reviewed as an RPC wire contract, not merely a C API change.
2. Optional business idempotency: a new `call()` is a new operation. Do not expose
   internal numbering again as a business retry key or promise permanent exactly-once
   execution without a separate state/persistence design.
3. Measure H7 latency and encode/match CPU time before compressing metadata or
   consolidating repeated decoding. Preserve explicit borrowing lifetimes.

Review [getting started](getting-started.md), then [RPC](tutorial-rpc.md).
Precise constraints are in the [RPC contract](rpc-runtime.md).
