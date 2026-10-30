# Generated endpoint UDP regression

Three Ztest cases use the default C kernel, generated Add/telemetry API and two
real Zephyr UDP sockets. They verify eight bidirectional concurrent calls, strict
four-slot admission, request snapshots, owned response after close, ordinary
telemetry, distinct business rejection, cancellation, uint32 clock-wrap timeout,
late response suppression, CLOSED completion, stale handle rejection and finite
idle/latched notification waits. No old engine or Python protocol implementation
is linked. A timeout/cancel can still leave a remotely executing request.

Run from a Zephyr workspace with matching WLC 0.13.0:

```sh
west twister -T /path/to/wirelink/samples/zephyr/udp_validation \
  -p native_sim -p native_sim/native/64 -p qemu_cortex_m3 \
  --extra-args WIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

Old reliable/CRC selector/replay/inspect-release cases belong to the removed
execution contract. Wire primitives retain independent frozen vectors; adapter
socket/guard/peer/backpressure/stop contracts run separately in tests/zephyr.
This suite validates generated API integration on the current native/NONE path.
