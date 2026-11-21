# Call a calculator over UDP

Enable `WIRELINK_BUILD_WLC_EXAMPLES`, `WIRELINK_BUILD_ASIO_UDP_ADAPTER`, and
`WIRELINK_BUILD_PLATFORM` with WLC codegen contract 2. CMake generates each role separately.
Run `calculator_server` in one terminal and `calculator_client` in another.
The default IPv4 loopback ports are 50003 / 50004. Override them with
`LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`.

`wirelink_rpc_lifecycle` is the independent in-memory regression for cancellation,
timeouts and lease drain. It builds with WLC examples enabled.
