# Send telemetry with two self-contained nodes

Configure with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and WLC 0.14.0, then run
`wirelink_telemetry_demo` or its CTest case. `telemetry.wl` declares the message,
`pub` sender and `sub` receiver. CMake uses `ALL_NODES` because this program hosts
both nodes; a firmware containing only the publisher uses `NODE pub`.

The publisher implements `pub_tx_write` by passing a complete packet to
`sub_rx_packet`. Both nodes own their ports and receive buffers. Business code
initializes ordinary required fields directly and implements `sub_on_telemetry`.
Advance the sender before admitting its next ordinary message: there is one TX
slot. The callback's decoded view lasts only through that callback; retain a copy
if the application needs the last value.
