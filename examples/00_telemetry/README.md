# Send telemetry over UDP

The default build includes both peers when CMake finds standalone Asio headers
and a WLC compiler with codegen contract 1. CMake generates each role separately.
Run `telemetry_subscriber` in one terminal and `telemetry_publisher` in another.
The default IPv4 loopback ports are 50001 / 50002. Override them with
`LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`.
