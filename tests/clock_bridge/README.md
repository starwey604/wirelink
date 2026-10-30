# C-owned kernel clock bridge

Enable `WIRELINK_BUILD_FOREIGN_TESTS=ON` with matching local WLC 0.13. The C
shared library owns both kernels, ports, C11 atomic objects and loopback storage.
C++ and Python use opaque exported C handles; Python callbacks are not involved.
The test wrapper allocates once at create and frees after close/quiescence.

One outer owner-clock read is shared by both kernel steps in each pass. Admission
also reads the clock once outside the kernel. Manual/native clocks, a long idle
gap, business rejection, a wrapping deadline, idempotent close and owned results
after close are exercised. The timeout case stalls the peer owner; the removed
deferred reply API and cache TTL are not recreated. The kernels/codecs perform
zero clock-provider reads, checked directly by the C wrapper.

`wirelink_clock_benchmark` emits `clock_bridge_v2`: one pass includes the C bridge,
two kernel steps, loopback service and one explicit outer clock read. It is not
the old single-endpoint/pump workload and its timing must not be compared with
those rows. Zephyr's endpoint_clock sample separately checks direct core steps
with zero clock reads. No CPU improvement claim is made by these bridge tests.
