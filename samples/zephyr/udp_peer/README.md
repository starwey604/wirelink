# Two-device UDP peer

The client/server use the same business schema, managed tagged RPC wire and
native_packet/NONE profile as the [desktop peers](../../../examples/05_zephyr_udp/README.md).
Both RPC roles remain in each generated kernel. Add is a short typed handler;
ordinary telemetry runs at an application-selected 20 ms period.

Configure a board's Ethernet/Wi-Fi driver, IPv4 address, network buffers and a
real initialization-time fresh-instance source before opening the adapter. The
sample uses `net_config_init` outside Wirelink. It defaults to server
`192.0.2.2/24:49101`, peer `192.0.2.1:49100`; these documentation-range addresses
must be replaced or provisioned on your test network. No discovery is performed.

```sh
west build -s /path/to/wirelink/samples/zephyr/udp_peer -b YOUR_BOARD \
  -d build/udp-server -- -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
west build -s /path/to/wirelink/samples/zephyr/udp_peer -b YOUR_OTHER_BOARD \
  -d build/udp-client -- -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc \
  -DEXTRA_CONF_FILE=client.conf
```

Use matching WLC 0.13.0; override Kconfig peer/local addresses and ports explicitly.
`sim.conf` supplies a test random source only for simulation. Do not use it for
product identities. The desktop client accepts explicit local/peer IPv4/ports and
can be used while validating one board. Expected client output contains
`20 + 22 = 42` and `telemetry sample=...`.

C owns the core, packet port and two RX units with a guard byte. Driver I/O and
typed callbacks run on the application owner. Wait merges the kernel's deadline,
UDP readiness/retry hint and the next business action; no second RPC timer or
internal adapter thread exists. The shared example helper only advances/waits/
closes these objects. Callbacks copy response views before returning.

Restart requires stopping submissions/owner activity, closing the kernel,
stopping/joining actual I/O and waiting for `wl_kernel_quiescent`, then obtaining
a new nonzero instance identity before reusing storage. Pending calls terminate
with CLOSED. Do not migrate in-flight handles or copy the old identity. Local
cancel/timeout cannot roll back remote actions, and requests are not automatically
retried or deduplicated. Ordinary telemetry/status freshness and business retry/
idempotency belong to the application. Instance IDs and integrity checks do not
provide authentication.

The peer sample's native32/64 client/server configurations are build-only checks.
Actual generated RPC/telemetry, four-call admission, late responses, cancellation,
wraparound timeout and finite waits run in `samples/zephyr/udp_validation` over
real Zephyr UDP loopback. Desktop peers also run as separate real UDP processes.
These checks do not claim physical board/PHY interoperability or throughput;
that acceptance requires the actual network hardware and configuration.
