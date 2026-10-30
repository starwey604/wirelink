# Explicit communication instances

This standalone sample runs the same generated native packet kernel and cases as
`tests/session`. The owner obtains a fresh nonzero identity before initializing
its packet port and kernel. The kernel never calls an identity source or clock.

Cases cover source failure, zero and repetition (with output unchanged), distinct
identities from a shared source, client-only rebuild while the server remains
alive, old success and rejection packets with the same numeric operation as a
new call, stale cancellation handles, business rejection, 100 calls with zero
identity-source reads, and operation-ID exhaustion followed by fresh-instance
recovery. Completion callbacks copy borrowed responses into application-owned
values, retained after close. An explicit test-only private-state seed reaches
operation exhaustion without exposing a production configuration knob.

The synchronous RAM driver copies into the peer's two bounded RX units before
reporting SENT. It retains no physical source. A captured old request is executed
again: there is no framework replay deduplication or exactly-once guarantee.
Its old-instance reply cannot complete the new-instance call.

The previous rr/ru/ur/uu reliability matrix and deferred reply cases were removed
because those execution capabilities no longer exist. This sample exercises one
explicit native packet deployment, with both client and server APIs available.
Actual asynchronous adapter drain/stop contracts are tested separately.

From an initialized Zephyr workspace, with matching local WLC 0.13:

```sh
west build -s /path/to/wirelink/samples/zephyr/session -b native_sim \
  -d /path/to/build/session-native -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

Twister runs native_sim, Cortex-M3, RISC-V and x86_64 QEMU. Simulators without a
real CSPRNG explicitly report the platform source as unavailable and exercise the
injected deterministic source. On ESP32-S3 the sample requires the native CSPRNG.
The 128-value native source check and endpoint reinitialization check are
functional checks, not statistical randomness or global uniqueness proofs.
No hardware flash is required by the simulator tests.
