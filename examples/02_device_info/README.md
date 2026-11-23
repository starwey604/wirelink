# Read device information over UDP

The default build includes both peers when CMake finds standalone Asio headers
and a WLC compiler with codegen contract 1. CMake generates each role separately.
Run `device_info_server` in one terminal and `device_info_client` in another.
The default IPv4 loopback ports are 50005 / 50006. Override them with
`LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`.
