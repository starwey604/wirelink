# Portable kernel fixtures

The root CTest build runs the production C11 kernel/port/wire/stream sources.
The concurrent packet-port and byte-FIFO fixtures keep all C atomic storage and
producer/consumer code in C. `threaded_fixture.cpp` only creates and joins a
portable `std::thread` through exported C functions; it does not substitute
`std::atomic` or assume a C/C++ atomic ABI. The packet test transfers 100000
RX/TX units, including duplicate-old-completion races; the byte test transfers
one million bytes before checking framing/gap recovery.

GCC/Clang can run these fixtures directly, including Clang ThreadSanitizer.
MSVC builds retain `/utf-8` and require a compiler with C11 atomic support;
`/experimental:c11atomics` propagates to C consumers through the installed
header targets. That flag is not applied to C++ consumers. Windows execution
still needs its own CI runner; a Linux run is not Windows qualification.

Generated node fixtures use a matching local WLC 0.14. `wl_packet_port_t` is a
complete, standard-layout type in C++20; C++ may embed it, while atomic access
remains in the C11 implementation. `packet_port_cpp.cpp` compares size, alignment
and every member offset against C, then exercises concurrent RX/TX, quiescent
close and in-place reinitialization of a C++-owned port. The byte FIFO remains
opaque to C++. The separate installed-consumer, foreign-bridge, Zephyr and
adapter suites test their actual ownership boundaries rather than sharing a second RPC implementation.
