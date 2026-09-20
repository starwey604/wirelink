# RPC platform, storage and CPU probes

A standalone validation app, not product firmware. Two Zephyr tasks exchange
native packets through bounded RAM queues with latched data/credit notifications.
One task owns each endpoint. This is not USB/UART/DMA hardware validation.

The sample checks one/four-slot endpoint creation from a two-block pool, allocation
and init failure rollback, pool exhaustion, 100 synchronous string-bearing calls,
rejection, a 2023-byte response, 20 asynchronous completions, and a separate task
interrupting a long synchronous wait. Both owners stop/join before destruction;
copied responses remain valid, and no endpoint allocator is called during RPCs.

It reports cycle-counter frequency, endpoint bytes, wait counts and unused stack.
`scheduled_RAM` measures 100 round trips with interrupts/scheduling enabled and a
completion-containing owner pass, not isolated RPC completion CPU cost. Separate
bounded IRQ-off batches measure an idle step, encode/decode of 2023 bytes and
volatile full-value copy after other tasks have joined. No subtractive timer
overhead correction is applied. The IRQ-enabled round-trip measurement uses the
system cycle clock to include sleep. Simulator cycles and stacks are not hardware
measurements; QEMU numbers are functional diagnostics only.

## Build and run

From an initialized Zephyr workspace:

```sh
west build -s /path/to/wirelink/samples/zephyr/rpc_platform -b native_sim \
  -d /path/to/build/rpc-platform-native -- \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

Repeat with a separate directory and `-DWIRELINK_RPC_TEST_CAPACITY=1`. The sample
matrix runs `native_sim`, RISC-V and x86_64 QEMU.

## ESP32-S3 hardware run

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/rpc_platform -d build/rpc-platform-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
west flash -d build/rpc-platform-hw
python benchmarks/zephyr/capture_serial.py \
  --port /dev/ttyACM0 --out rpc-platform.log --until "ALL PASS" --seconds 120
```

`boards/esp32s3_devkitc_esp32s3_procpu.overlay` routes the console to the board
USB Serial/JTAG port. Record the board clocks/cache configuration, RAM and build
flags with the result, and do not compare hardware numbers with simulator numbers.
