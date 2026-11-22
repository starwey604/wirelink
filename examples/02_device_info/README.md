# Read device information over UDP

Enable `WIRELINK_BUILD_WLC_EXAMPLES`, `WIRELINK_BUILD_UDP_ADAPTER`, and
`WIRELINK_BUILD_PLATFORM` with WLC codegen contract 1. CMake generates each role separately.
Run `device_info_server` in one terminal and `device_info_client` in another.
The default IPv4 loopback ports are 50005 / 50006. Override them with
`LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`.
