# Desktop peers for the Zephyr UDP example

`client.c` and `server.c` use the same generated schema/profile as the Zephyr
applications and native-loopback regression. Start with the desktop pair, then
replace either process with a board. No Python codec or hand-built frame is used.

See the [two-device walkthrough](../../samples/zephyr/udp_peer/README.md)
([中文](../../samples/zephyr/udp_peer/README-cn.md)) for build commands, addresses,
expected output, connection restart and the outstanding hardware acceptance gate.
