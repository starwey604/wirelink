# Installed-contract checks

Install a fresh Wirelink 0.13 prefix first. The C-only `c_core` project verifies
CMake and pkg-config without enabling C++. `consumer` compiles the public C and
C++20 headers and links the independent wire/diagnostic tools. C++ sees packet
port/byte-FIFO atomic storage only through opaque pointers. Add
`-DWIRELINK_CONSUMER_HOST=ON` to exercise the explicitly loaded host component
with a C-owned kernel and a real executor owner/join.

`wlc_current` builds installed native, COBS, C++ opaque-owner and keyword-codec
consumers, then verifies rejection of v1 profiles, the removed separate runtime,
missing compilers and obsolete compiler versions. It requires a matching local
WLC 0.13 binary. `cpp_session` tests the installed owning wrapper.

`udp_consumer` exercises the installed packet UDP target with C-owned storage,
latched waiting and stop. `packet_adapters` runs the full installed packet UDP,
controlled USB and PTY serial contracts, plus missing-component/backend negative
checks. Supply the unchanged Astrial backend source explicitly; these tests do
not flash or open the board's native USB device. The serial PTY case uses a
controlled physical-drain query and is not a hardware UART qualification.

The calculator/device consumer projects require installed SDK 0.13 packages.
`check_generated_sdks.py` compares checked-in output with matching WLC, while
`check_sdk_wheels.py` creates an isolated environment, installs both wheels
without network/dependencies/compiler PATH, and exercises sync/async calls with
independent C peers. Errors retain separate outcome/status/OS fields. A business
rejection is not a transport error. Local wheel tests do not claim all Python
ABIs or manylinux qualification.

`build_manylinux_sdks.sh` runs only inside the specified manylinux container and
requires `WIRELINK_WLC_EXECUTABLE` to name a matching executable **inside** that
container (for example a separately built static musl compiler). It does not
publish artifacts. The platform completion/timeout scripts compile unchanged
external driver snapshots against controlled fixtures; those are separate from
the Wirelink package and never edit vendor sources.
