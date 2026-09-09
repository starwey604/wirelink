# Static RPC, telemetry and bulk services

One generated `product_endpoint_t` per peer owns one context, one adapter and
one dispatch loop. `product.wl` imports independently maintained `arm.wl` and
`upgrade.wl`; their profiles compose at generation time. No dynamic registry,
USB channel or wire-frame revision is involved.

Build with the matching WLC 0.7.0-dev / ABI 32:

```sh
cmake -S . -B build/composed -DWIRELINK_BUILD_COMPOSED_SERVICES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
cmake --build build/composed
ctest --test-dir build/composed --output-on-failure
```

The executable transfers 4096 bytes through the real bulk sender/receiver,
generated borrowed `direct` handlers and loopback transport, while Query RPC
and telemetry remain active. It injects one sink BUSY (no bytes consumed) and
one dropped application Status, checks byte-for-byte/CRC completion, waits for
the reliable Reboot response ACK, and closes during another upload to verify
exactly-once abort. It never actually reboots, operates actuators or writes flash.

`wl_endpoint_set_services()` binds borrowed static hooks before driving starts.
The receiver maps typed data into `wl_bulk_receiver_*`; no raw RX ownership
escapes the generated dispatcher. Each progress hook attempts at most one
message, defers on backpressure, and supplies a deadline. A peer-session hook
resets bulk and calls generated `product_runtime_peer_observe()` so a direct
message can establish the new peer before its next RPC. RPC has first admission
to the shared reliable TX slot: products must bound RPC and telemetry load
during upload. Service rotation does not guarantee bandwidth under overload.

`on_response_terminal` only observes link-level ACK/timeout/failure. Record a
success flag and reboot at a safe point after the endpoint pass returns;
do not take the observed handle or reboot in the callback. Link ACK does not
mean the host application has processed the reply. Cached replay may notify
again, so a product's reboot intent must be idempotent and session-scoped.

The message IDs and `BulkCommand` shape here are an example mapping, not a new
standardized bulk wire protocol. Preserve existing FCI upgrade payloads when
applying this architecture to a product. MCUboot slot writing, signature checks,
test/confirm/revert and reconnect are outside this example.

The same source runs on Zephyr via
[`samples/zephyr/composed_services`](../../samples/zephyr/composed_services/).
Hardware and migration notes: [Chinese handoff](../../docs/composed-services-cn.md).
