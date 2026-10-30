# Kernel owner command/stop model

[中文](README-cn.md).

This is a deterministic scheduling check, not a CPU benchmark. It needs the
current host component, with no WLC, Asio, Google Benchmark or hardware:

```sh
cmake -S . -B build/owner-pass -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_OWNER_PASS_TESTS=ON
cmake --build build/owner-pass --parallel
ctest --test-dir build/owner-pass --output-on-failure
```

A test-only first command blocks the owner while the caller fills all 32 queue
slots and observes rejection of a 33rd admission. One producer publishes an RX
unit through the actual C-owned packet port. After release, the current eight
command budget gives the kernel a pass: the first seven queued commands see no
RX handler yet (the barrier consumed one command), and command eight observes
that handler. Counts prove available-work fairness without a wall-time threshold.
The second case requests stop while the barrier holds the owner, then proves
that all 32 queued commands are rejected on the owner, no queued work executes,
I/O stop runs once and the C kernel is quiescent after join.

C owns the kernel/port/atomic storage; C++ only supplies the real executor and
external test counters. No generic service, RPC table or activity-counter API is
restored. JSON rows use `wirelink-owner-kernel-v1`. Startup service counts are
racy diagnostics; queue bounds, delivery and RX progress are exact contracts.
The old `WIRELINK_HOST_ACTIVITY` knob explicitly errors. The separate core and
adapter suites cover physical TX/RX ownership; owning RPC wrappers run in the
Session tests. This command-queue model does not claim end-to-end RPC timing.
