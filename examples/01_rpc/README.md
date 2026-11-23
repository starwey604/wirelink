# Call a calculator over UDP

The default build includes both peers when CMake finds standalone Asio headers
and a WLC compiler with codegen contract 1. CMake generates each role separately.
Run `calculator_server` in one terminal and `calculator_client` in another.
The default IPv4 loopback ports are 50003 / 50004. Override them with
`LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`.

`wirelink_rpc_lifecycle` is the independent in-memory regression for cancellation,
timeouts and lease drain. It builds when `BUILD_TESTING=ON` and a compatible WLC is available.
