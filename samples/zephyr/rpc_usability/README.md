# Typed RPC ownership on Zephyr

Two generated endpoints use the same C kernel on one owner, over packet
loopback. The sample verifies four accepted requests and fifth-call rejection,
request snapshots, callback-submitted chaining, reverse business rejection,
bounded UTF-8/NUL response copies, a 2023-byte owned response after close,
uint32-wrap timeout while the physical source remains borrowed, real driver
cancellation/drain and stale handle rejection after fresh-instance reinit.

The physical transfer cancelled at shutdown reports I/O error independently of
an already delivered logical timeout. The sample accepts that error only for a
known pending model-driver borrow and still requires physical quiescence and no
second callback. There is no fake success/drain, replay cache, inspect/release
terminal slot, deferred reply or internal core clock.

```sh
west twister -T /path/to/wirelink/samples/zephyr/rpc_usability \
  -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64 \
  --extra-args WIRELINK_WLC_EXECUTABLE=/path/to/matching/wlc
```

The current contract always has four call/four reply slots. The old one-slot
capacity knob is explicitly rejected. All four listed platforms now execute the
same full four-slot, 2023-byte-response scenario, including the 64-KiB Cortex-M3.
No schema bound is reduced for that platform. Expected marker:
`RPC_OWNERSHIP ALL PASS`. This is functional target validation, not a physical
USB/UART test or an RPC latency benchmark.
