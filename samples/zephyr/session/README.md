# Automatic sessions

This standalone sample is not product firmware. It checks the platform identity
source and endpoint reinitialization, then runs the same deterministic RPC matrix
as `tests/session`: reliable/reliable, reliable/unreliable, unreliable/reliable
and unreliable/unreliable.

Cases cover source failure/zero/repetition, shared configuration, client-only
rebuild with same-number old success/rejection injection, deferred replies,
operation-ID exhaustion/recovery and zero identity-source reads on the RPC hot
path. Transport is bounded RAM packet queues, not USB/UART/DMA. Generated values
and core code are shared with host tests. Simulators without a real CSPRNG
explicitly report the platform check as unavailable and exercise the injected
source instead.

## Build and run

From an initialized Zephyr workspace with a matching WLC compiler:

```sh
west build -s /path/to/wirelink/samples/zephyr/session -b native_sim \
  -d /path/to/build/session-native -- \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

Twister runs `native_sim`, Cortex-M3, RISC-V and x86_64 QEMU.

## ESP32-S3 hardware run

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/session -d build/session-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
west flash -d build/session-hw
python benchmarks/zephyr/capture_serial.py \
  --port /dev/ttyACM0 --out session.log --until "ALL PASS" --seconds 60
```

On ESP32-S3 the sample requires the on-chip CSPRNG. Expect
`SESSION platform RNG 128 PASS`, `SESSION platform endpoint reinit PASS`, all four
matrix passes and `SESSION ALL PASS`. RNG sampling is a functional check, not a
statistical proof of randomness or unique IDs.
