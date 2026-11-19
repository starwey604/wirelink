# Bounded owned response

Build `wirelink_device_info_demo` with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and a
matching WLC 0.0.99. `device_info.wl` declares the embedded `info_client` and
`info_server`; the server accesses application state through `self->user`. Eight real loopback RPCs return bounded UTF-8/NUL data and a
query counter. The completion copies its borrowed view into the generated owned
value before returning. The saved result remains valid after close and physical
quiescence. Requests and replies use the shared C endpoint; no inspect/release
terminal slot or second synchronous RPC implementation is involved.
