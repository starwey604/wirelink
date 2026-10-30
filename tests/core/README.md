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

Generated endpoint fixtures use a matching local WLC 0.13. The public packet
port and byte-FIFO atomic representations are opaque to C++. The separate
installed-consumer, foreign-bridge, Zephyr and adapter suites test their actual
ownership boundaries rather than sharing a second RPC implementation.
