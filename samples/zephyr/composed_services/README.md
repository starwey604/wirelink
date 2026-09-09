# Composed service target test

Runs the [host executable specification](../../../examples/04_composed_services/README.md)
unchanged on Zephyr. Two endpoints communicate through in-memory loopback;
this does not validate USB Bulk, actual flash programming or MCUboot swaps.
Expected console marker: `COMPOSED_SERVICES PASS`.

Build from an initialized Zephyr workspace:

```sh
west build -s /path/to/wirelink/samples/zephyr/composed_services -b native_sim \
  -d /path/to/build/composed-native -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
```

For the internal H723 board use `-b dm_mc02/stm32h723xx` and
`-DBOARD_ROOT=/path/to/Ragtime_Firmwares/firmware`. The board configuration
disables CAN, PWM, LEDs and UART; logs use SEGGER RTT channel 0. If the west
workspace contains application-specific modules, explicitly limit
`ZEPHYR_MODULES` to `cmsis_6`, `hal_stm32` and `segger` directories; Wirelink is
added by this sample. This avoids pulling the Ragtime application into the test.

This image starts at `0x08000000`, not an MCUboot slot. J-Link flashing replaces
the sectors occupied by this standalone image, including an existing bootloader
there. It is not an in-application update. Flash only an authorized test target;
do not use mass erase or alter option bytes. After flashing, the board remains
on this inert test image until another image is programmed.
