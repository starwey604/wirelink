# Single-byte varint product screen

This screen tests a WLC change derived independently from original A: the common
64-bit reader returns directly for a complete byte below 128. Multibyte parsing,
wire semantics, error/cursor/output contracts and existing entry points remain
unchanged. The shared template affects both ordinary codecs and the private RPC
codec header. It does not include the unpromoted required-fixed encoder.

The execution record is `/home/ww/codings/wirelink-varint-product-20261111/`.
Product sources are the frozen original application/core/observer inputs in
`wirelink-required-fixed-20261101/inputs/`, with the new WLC. Both A and V use
the retained gated A host within each observer mode. Capture metadata calls
candidate V **B**; its actual firmware IDs are `varint-20261111-{off,on}`.

```sh
python -B benchmarks/notification_progress/varint_product/analyze.py \
  --evidence /absolute/wirelink-varint-product-20261111 \
  --out /absolute/new-replay.json
python -B -m unittest discover \
  -s benchmarks/notification_progress/varint_product -p 'test_*.py'
```

Replay needs raw captures, manifests, registered tools, state/environment
timelines, the build registry and text machine-code receipt, not deleted builds
or compiler binaries. It validates four smoke and two boundary qualifications,
then three AB/BA/AB pairs per observer. Transitions exclude whole pairs, without
replacement; lock, display, boot and power state all distinguish comparison
strata. Every 1000-call Sample distribution and the cold call remain visible.

The primary benefit is a stable >5% reduction in CPU-on H2D MCU active time per
equivalent frame. Window CPU fractions are divided by concurrent goodput/32;
this is not individual-frame tracing. Existing 5% CPU/latency, 95% goodput and
0.5pp idle protections remain. Stability requires three eligible same-state
pairs and each series span <=5% (idle <=0.5pp). Host stream CPU is also retained
as a supplementary window-normalized observation. A 32B screen is not the full
O5 A/B/C acceptance matrix or final cache-mechanism attribution.

The controller leaves the last healthy tested firmware installed. Replay checks
that the handoff matches the final capture's image, observer and product identity,
with no automatic restoration or closing smoke. Permanently delete unused build
trees/Rust targets after retaining source, raw data and text evidence; do not
archive compiled payloads. Keep the needed original A recovery/reference assets.
