# H1: ordinary RPC ownership on Zephyr

This is a standalone target-CPU functional check, not product firmware or a
host-to-board transport test. Both endpoints run on the same owner over loopback.
It does not drive actuators or change persistent storage. Authorized H7 execution
on 2026-09-07 passed both capacities, including a system-reset rerun and a
user-confirmed power cycle of each. H1 is complete; no implementation changes
were needed. Evidence, probe-recovery history and limitations are in the
[H7 validation record](../../../docs/rpc-h1-h7-validation-cn.md).

The sample covers bounded strings, a 2031-byte response, request snapshotting,
100 consecutive operations within cache TTL, full-capacity bursts, BUSY admission,
one-slot callback chaining, rejection, cancellation, queue deadlines across uint32
wrap, failed admission, close notification and stale handles. Saved responses
survive slot reuse and endpoint reinitialization. Clock-read budgets are asserted.
The deterministic phase uses an injected manual clock. A separate phase uses
Zephyr uptime after a 75-ms idle gap; its 2000-ms deadline is a functional margin,
not an RPC latency benchmark. The existing `endpoint_clock` sample additionally
checks real deadlines and remains an advanced-token regression.

## Build

From an initialized Zephyr workspace, with absolute paths adjusted to your machine:

```sh
west build -b dm_mc02/stm32h723xx /path/to/wirelink/samples/zephyr/rpc_usability \
  -d /path/to/wirelink/build/rpc-m2-h7 -- \
  -DBOARD_ROOT=/path/to/Ragtime_Firmwares/firmware \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/matching/wlc
```

The external board root supplies only the board definition, not a product
dependency migration. Use matching ABI 23 WLC. Repeat in a separate build directory
with `-DWIRELINK_RPC_TEST_CAPACITY=1`; the default is four slots. This sets capacity
consistently for all generated-runtime consumers.

Native/QEMU validation uses `west twister -T samples/zephyr/rpc_usability` from the
Wirelink repository or the equivalent absolute path. The 64-KiB Cortex-M3 supports
the one-slot stress configuration only: two large four-slot endpoints plus Zephyr
exceed its RAM. Four slots are exercised on native_sim, RISC-V and x86_64 QEMU.

## H7 handoff checklist

1. Record matching Wirelink/WLC commits, ABI, ELF hash, board, clocks and build flags.
2. When H1 is authorized, flash the standalone image and capture RTT output.
   No old-firmware backup is required. Ask for replug/held RESET only if J-Link
   connection fails; the sample itself requires no buttons.
3. Require `RPC_H1 ABI=23 capacity=... start`, no FAIL/abort, the final size/count
   record, and `RPC_H1 ALL PASS`. Repeat both capacities and cold restarts.
4. A successful loopback check proves target execution/ownership, not USB/UART/DMA
   performance, hardware transport reliability or product compatibility. Those
   require later H2/H3 work; do not use host timings as firmware CPU measurements.
