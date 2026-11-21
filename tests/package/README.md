# Installed-contract checks

Install Wirelink to a fresh prefix first. The C-only `c_core` project verifies
CMake and pkg-config without enabling C++. `consumer` compiles the public C and
C++20 headers and links the independent wire/diagnostic tools. C++ verifies that
`wl_packet_port_t` is complete and standard-layout; its atomic operations remain
C-owned. `wl_byte_fifo_t` remains opaque. Add
`-DWIRELINK_CONSUMER_HOST=ON` to exercise the explicitly loaded host component
with a C-owned endpoint and a real executor owner/join.

`wlc_current` builds installed native, COBS, C++ opaque-owner and keyword-codec
consumers. `NODE client` and `ALL_NODES` additionally exercise single-role linking
and a multi-node loopback with default linker options, without section GC. The
negative checks reject removed `PROFILE` and runtime helpers, missing compilers
and incompatible generated-code contracts. Use a WLC binary that reports codegen contract 1. `cpp_session` tests the installed owning wrapper.

`udp_consumer` exercises the installed packet UDP target with C-owned storage,
latched waiting and stop. `packet_adapters` runs the full installed packet UDP,
controlled USB and PTY serial contracts, plus missing-component/backend negative
checks. Supply the unchanged Astrial backend source explicitly; these tests do
not flash or open the board's native USB device. The serial PTY case uses a
controlled physical-drain query and is not a hardware UART qualification.

Build and install both SDKs separately, then configure and test the
calculator and device consumer projects against that install prefix.
The bindings CI also builds both wheels and runs their Python tests against
independent C peers.

The platform completion/timeout scripts compile external driver snapshots
against controlled fixtures.
