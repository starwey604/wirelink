# Telemetry over the static kernel

Configure the repository with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and a matching
`WIRELINK_WLC_EXECUTABLE`. Run `wirelink_telemetry_demo` or its CTest case.
The publisher and subscriber are two generated endpoints in one deterministic
application owner loop. The profile declares both send and direct routes.
Admission copies each message; the subscriber borrows its decoded view only for
the callback. There is no retained LATEST mailbox in the kernel. A production
application can keep its own last value if needed. No packet reliability is implied.
