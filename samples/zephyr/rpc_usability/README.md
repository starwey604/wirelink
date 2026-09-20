# Ordinary RPC ownership on Zephyr

This is a standalone target-CPU functional check, not product firmware or a
host-to-board transport test. Both endpoints run on the same owner over loopback.
It does not drive actuators or change persistent storage.

The sample covers bounded strings, a 2023-byte response, request snapshotting,
100 consecutive operations within cache TTL, full-capacity bursts, BUSY admission,
one-slot callback chaining, rejection, cancellation, queue deadlines across uint32
wrap, failed admission, close notification and stale handles. Saved responses
survive slot reuse and endpoint reinitialization. Clock-read budgets are asserted.
The deterministic phase uses an injected manual clock. A separate phase uses
Zephyr uptime after a 75-ms idle gap; its 2000-ms deadline is a functional margin,
not an RPC latency benchmark. The `endpoint_clock` sample additionally checks real
deadlines and remains an advanced-token regression.

## Build and run

From an initialized Zephyr workspace with a matching WLC compiler:

```sh
west build -s /path/to/wirelink/samples/zephyr/rpc_usability -b native_sim \
  -d /path/to/build/rpc-ownership-native -- \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

Repeat in a separate build directory with `-DWIRELINK_RPC_TEST_CAPACITY=1`; the
default is four slots. This sets capacity consistently for all generated-runtime
consumers.

Twister runs `native_sim`, Cortex-M3, RISC-V and x86_64 QEMU. The 64-KiB Cortex-M3
supports the one-slot stress configuration only: two large four-slot endpoints
plus Zephyr exceed its RAM. Four slots are exercised on `native_sim`, RISC-V and
x86_64 QEMU.

## ESP32-S3 hardware run

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/rpc_usability -d build/rpc-ownership-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
west flash -d build/rpc-ownership-hw
python benchmarks/zephyr/capture_serial.py \
  --port /dev/ttyACM0 --out rpc-ownership.log --until "ALL PASS" --seconds 120
```

`boards/esp32s3_devkitc_esp32s3_procpu.overlay` routes the console to the board
USB Serial/JTAG port. Expect `RPC_OWNERSHIP ALL PASS`, no FAIL/abort, and the
final size/count record. A successful loopback check proves target execution and
ownership, not USB/UART/DMA performance or hardware transport reliability.
