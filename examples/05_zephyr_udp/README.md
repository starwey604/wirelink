# Desktop UDP peers

C business code uses the same `udp_demo.wl` as the Zephyr peers. The external
`udp_client` node calls Add and receives telemetry; `udp_server` serves Add and
sends telemetry. Each executable generates only its selected node, so it does not
need to implement the other role's handlers. Native packet framing preserves UDP
packet boundaries.

```sh
cmake -S . -B build/udp -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_BUILD_ASIO_UDP_ADAPTER=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching/wlc
cmake --build build/udp --parallel
ctest --test-dir build/udp -R wirelink_udp_peer --output-on-failure
```

The two executables accept `LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT` arguments.
Start the server with the addresses/ports reversed relative to the client. The
client prints `20 + 22 = 42` and a telemetry sample. The test reuses a client
address/port across three new processes and verifies absent-peer timeout.

C owns the endpoint, port and two RX units. The desktop C++ helper only owns packet UDP and
wait resources: it initializes a fresh instance identity outside the core,
services I/O and advances the same endpoint on the calling owner, and merges
waiting for the endpoint and I/O without an RPC registry or thread. Admission encodes before
return; completion copies the borrowed response. Close stops actual I/O, steps
until physical quiescence, then frees socket resources.

See the [Zephyr peer walkthrough](../../samples/zephyr/udp_peer/README.md) for
board network provisioning. Software loopback does not establish physical
Ethernet/Wi-Fi performance or validate a particular board/PHY.
