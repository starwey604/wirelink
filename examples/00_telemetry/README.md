# Send telemetry over UDP

Enable `WIRELINK_BUILD_WLC_EXAMPLES`, `WIRELINK_BUILD_ASIO_UDP_ADAPTER`, and
`WIRELINK_BUILD_PLATFORM` with WLC codegen contract 2. CMake generates each role separately.
Run `telemetry_subscriber` in one terminal and `telemetry_publisher` in another.
The default IPv4 loopback ports are 50001 / 50002. Override them with
`LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`.
