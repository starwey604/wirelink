# Explicit owner clock check

The generated calculator endpoint runs over packet loopback on one Zephyr owner.
The owner reads real uptime and supplies `now`; admission, codecs and the shared
C kernel do not read a clock. First submission after 75/150 ms idle, short reply,
business rejection and a missing-handler timeout are checked. Missing handler
returns NOT_SUPPORTED from the owner pass; the client still waits until its
local deadline. No deferred reply or retry engine is involved.

The final 20,000 idle kernel passes reuse a supplied timestamp and assert zero
core clock reads. Cycles are diagnostic, with interrupts enabled; native/QEMU
values are not hardware performance measurements or a comparison with the old
clock-reading runtime. The fixture supplies a fresh identity at each reinit and
closes/drains actual packet borrows before reusing storage.

```sh
west twister -T /path/to/wirelink/samples/zephyr/endpoint_clock \
  -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64 \
  --extra-args WIRELINK_WLC_EXECUTABLE=/path/to/matching/wlc
```

Expected console marker: `CLOCK_CHECK ALL PASS`. This is a loopback contract
check, not USB/UART/CAN, actuator or firmware performance validation.
