# Two-device UDP example

[中文](README-cn.md). This is an ordinary application, not a Ztest: build either
[`src/server.c`](src/server.c) or [`src/client.c`](src/client.c). Each program has
one generated endpoint and one UDP adapter. The server implements Add and emits
50 Hz telemetry. The client requests `20 + 22` and reads a telemetry sample.

The desktop counterparts in [`examples/05_zephyr_udp`](../../../examples/05_zephyr_udp/)
and the [loopback regression](../udp_validation/) generate from that directory's
single schema/profile. Neither side hand-encodes frames or RPC metadata.

## Build and run

First install the matching standalone WLC and Asio as described in
[installation](../../../docs/installation.md).
For this development checkout use the WLC version/ABI pinned in
`cmake/WirelinkWlc.cmake`; a local `wlc/` worktree is not required.
From the Wirelink repository:

```sh
cmake -S . -B build/udp-peer -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build build/udp-peer --target udp_peer_client udp_peer_server --parallel 2
```

In separate terminals, first verify the desktop programs:

```sh
build/udp-peer/examples/05_zephyr_udp/udp_peer_server 127.0.0.1 49101 127.0.0.1 49100
build/udp-peer/examples/05_zephyr_udp/udp_peer_client 127.0.0.1 49100 127.0.0.1 49101
```

Arguments are `LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`. Expected client output
contains `20 + 22 = 42` and `telemetry sample=...`. Stop the desktop server with
Ctrl+C. On Windows use the corresponding `.exe` paths (and configuration folder
for multi-configuration generators).

For an Ethernet-capable Zephyr board, configure its PHY/pins/driver and real
entropy source first. This sample defaults to server, `192.0.2.2/24:49101`, with
fixed peer `192.0.2.1:49100`. These documentation addresses are for an isolated
test link; choose available addresses on your actual network. From an initialized
Zephyr workspace, replacing the board and absolute paths:

```sh
west build -b YOUR_ETHERNET_BOARD /path/to/wirelink/samples/zephyr/udp_peer \
  -d /path/to/build/udp-peer -- \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc
west flash -d /path/to/build/udp-peer
```

Then run the desktop client with `192.0.2.1 49100 192.0.2.2 49101`. Allow inbound
UDP on those ports in the host firewall; no broadcast or discovery is used.
For the reverse direction, build with `-DEXTRA_CONF_FILE=client.conf`, run the
desktop server with `192.0.2.1 49101 192.0.2.2 49100`, and start the board client.
The client exits after its functional check; restart it for another check.

Override `CONFIG_NET_CONFIG_MY_IPV4_ADDR`, `CONFIG_NET_CONFIG_MY_IPV4_NETMASK`,
`CONFIG_NET_CONFIG_MY_IPV4_GW`, and `CONFIG_SAMPLE_WIRELINK_UDP_PEER_ADDRESS` in
an application `.conf` fragment supplied through `EXTRA_CONF_FILE`. Ports and
role are sample configuration, not switches scattered through business code.
The network helper checks initialization before creating the endpoint.

For Linux native_sim, use `-b native_sim/native/64 -DEXTRA_CONF_FILE=sim.conf`
(the latter after `--`) and set up a TAP interface using the official
[native_sim networking guide](https://docs.zephyrproject.org/latest/services/connectivity/networking/native_sim_setup.html).
For the client use `-DEXTRA_CONF_FILE="sim.conf;client.conf"`.
Keep native sockets: socket offload is unsupported. `sim.conf` enables an
insecure simulation entropy source: **never include it in a hardware build**.
CI builds both roles but does not create TAP devices or claim network execution.

## Reuse one endpoint when reconnecting

Stop new submissions and the owner, close the **generated endpoint** (pending
RPC callbacks receive cancellation), join old notification producers, then close
the old adapter. Reinitialize the same endpoint storage with the normal platform
environment and open the new adapter/configuration. Initialization obtains a new
session identity; do not copy the old endpoint or identity. This is a connection
restart, not migration of in-flight calls. Do not automatically replay commands
with side effects whose outcome is unknown.

The regression verifies this UDP close/reopen sequence, including rejection of
an old response after reinitialization. It is not a tested USB↔UDP switch or a
transport-manager API. Switching stream/native profiles may require compatible
generated layouts or an application-owned union, even when only one endpoint
is active. Coordinate both peers and stop/drain old traffic first.

Session IDs and CRC are not authentication or a universal replay filter. Old
requests and unreliable telemetry need application-specific freshness policy.
`latest` means the most recently received value, not necessarily the newest
sample at the sender; include a sample counter/time if order matters.

## Acceptance boundary

See the [adapter contract](../../../docs/zephyr-udp.md) for ownership, budgets,
optional timing and the deferred Zephyr pool-exhaustion wait. This example is
not a hard-real-time guarantee. H5 Ethernet/DMA, physical link disconnects,
pool sizing and CPU/latency tests remain hardware acceptance work.
