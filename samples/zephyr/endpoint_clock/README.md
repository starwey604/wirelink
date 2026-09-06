# Default endpoint clock check

An isolated Zephyr application using the generated calculator endpoint and the
real `k_uptime_get_32()` clock. Two endpoints communicate through the in-memory
loopback adapter on the same CPU. It does **not** validate USB/UART/CAN transport,
actuator behavior, or a product firmware build.

It checks first submission after idle without a preparatory step, inline reply
and rejection, a real deferred-call timeout, exact clock-read budgets, and no
premature reliable retries. It then measures 20,000 idle endpoint passes with
`k_cycle_get_32()`; interrupts remain enabled, so the result includes interrupt
and timer overhead, not isolated CPU instruction time. Virtual native_sim time
can report zero cycles and is not a performance measurement.

Build from an initialized Zephyr workspace, using a separately installed ABI 21
compiler (replace all absolute paths):

```sh
west build -s /path/to/wirelink/samples/zephyr/endpoint_clock -b native_sim \
  -d /path/to/build/endpoint-clock-native -- \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

The internal H7 target is `dm_mc02/stm32h723xx`, with
`-DBOARD_ROOT=/path/to/Ragtime_Firmwares/firmware`. Its overlay disables CAN,
PWM, LEDs and UART, and sends console output through SEGGER RTT channel 0.
Expect `CLOCK_HIL ALL PASS`. Obtain the RTT control-block address from the
built ELF; [RTT Logger](https://kb.segger.com/J-Link_RTT_Logger) accepts
`-RTTAddress` and `-RTTChannel` for explicit selection.

This standalone image is linked at flash address `0x08000000`, **not** an MCUboot
slot. Do not flash it over a product bootloader without backing up and arranging
restoration. Identify the connected chip/probe, preserve the complete existing
internal flash, and verify the backup before loading the test. Do not erase all
flash, change option bytes, touch external storage, or alter USB drivers. Restore
the previous image after testing. Hardware results belong in
[the clock evolution record](../../../docs/endpoint-clock-evolution.md).
