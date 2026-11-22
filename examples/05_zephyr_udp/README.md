# Desktop UDP peers

C business code uses the same `udp_demo.wl` as the Zephyr peers. The
`udp_client` node calls Add and receives telemetry; `udp_server` serves Add and
sends telemetry. Each executable generates only its selected node, so it does not
need to implement the other role's handlers. Native packet framing preserves UDP
packet boundaries.

```sh
cmake -S . -B build/udp -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_BUILD_UDP_ADAPTER=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching/wlc
cmake --build build/udp --parallel
ctest --test-dir build/udp -R wirelink_udp_peer --output-on-failure
```

The two executables default to localhost ports 50007 and 50008. To use another
address or port, pass `LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT` to each
executable and swap the local and peer arguments between the two. The
client prints `20 + 22 = 42` and a telemetry sample. The test reuses a client
address/port across three new processes and verifies absent-peer timeout.

Open the socket with `wl_udp_open`, then bind the generated node to
`wl_udp_transport(udp)`. The owner drives I/O and endpoint work with
`wl_node_step` and waits with `wl_node_wait` using a freshly sampled clock.
Admission encodes before
return; completion copies the borrowed response. Close stops actual I/O, steps
until physical quiescence, then frees socket resources.

See the [Zephyr peer walkthrough](../../samples/zephyr/udp_peer/README.md) for
board network provisioning. Software loopback does not establish physical
Ethernet/Wi-Fi performance or validate a particular board/PHY.
