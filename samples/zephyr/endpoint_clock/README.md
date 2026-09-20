# Default endpoint clock check

An isolated Zephyr application using the generated calculator endpoint and the
real `k_uptime_get_32()` clock. Two endpoints communicate through the in-memory
loopback adapter on the same CPU. It does **not** validate USB/UART/CAN
transport, actuator behavior, or a product firmware build.

It checks first submission after idle without a preparatory step, inline reply
and rejection, a real deferred-call timeout, exact clock-read budgets, and no
premature reliable retries. It then measures 20,000 idle endpoint passes with
`k_cycle_get_32()`; interrupts remain enabled, so the result includes interrupt
and timer overhead, not isolated CPU instruction time. Virtual `native_sim` time
can report zero cycles and is not a performance measurement.

## Build and run

From an initialized Zephyr workspace with a matching WLC compiler:

```sh
west build -s /path/to/wirelink/samples/zephyr/endpoint_clock -b native_sim \
  -d /path/to/build/endpoint-clock-native -- \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

For automated regression, run the same sample through Twister:

```sh
west twister -T /path/to/wirelink/samples/zephyr/endpoint_clock \
  -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64 \
  -x=WIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -x=WIRELINK_WLC_EXECUTABLE=/path/to/wlc \
  --inline-logs --outdir /path/to/build/endpoint-clock-twister
```

CI runs these four platforms with the matching source-built compiler. The console
harness requires all four RPC results, the clock-read budget result, and the final
success marker in order. It does not set a simulated-cycle performance threshold.

## ESP32-S3 hardware run

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/endpoint_clock -d build/endpoint-clock-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
west flash -d build/endpoint-clock-hw
python benchmarks/zephyr/capture_serial.py \
  --port /dev/ttyACM0 --out clock.log --until "ALL PASS" --seconds 60
```

`boards/esp32s3_devkitc_esp32s3_procpu.overlay` routes the console to the board
USB Serial/JTAG port. Expect `CLOCK_CHECK ALL PASS`. Hardware cycle counts are
real CPU time; `native_sim` cycles are not.
