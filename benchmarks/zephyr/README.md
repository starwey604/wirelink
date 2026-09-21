# Zephyr firmware benchmarks

[中文](README-cn.md).

These apps run the `benchmarks/` workloads on the ESP32-S3 DevKitC and report
cycles or wall time over the board's USB Serial/JTAG console. No J-Link or RTT
probe is required. The reference board is `esp32s3_devkitc/esp32s3/procpu`.

| App | Host counterpart |
| --- | --- |
| `codec_plan` | [codec_plan](../codec_plan/README.md) |
| `framing` | [framing](../framing/README.md) |
| `mixed_traffic` | [mixed_traffic](../mixed_traffic/README.md) |
| `rpc_validation` | [rpc_validation](../rpc_validation/README.md) |
| `rx_backend` | Zephyr-only RX ring test |

Each app selects `zephyr,console = &usb_serial` and enables `&usb_serial` in
`boards/esp32s3_devkitc_esp32s3_procpu.overlay`, so the firmware output and the
host `compare.py` logs use the same text format. A correct build reports
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.

## Build and flash

From an initialized Zephyr workspace, add this repository as a module and build
the app. For example `framing`:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/framing -d build/framing-hw -- \
  -DZEPHYR_EXTRA_MODULES=/path/to/wirelink
west flash -d build/framing-hw --esp-device /dev/ttyACM0
```

Flash over the board's UART bridge and capture over the USB Serial/JTAG
console, otherwise the two processes contend for the same port. On a board
whose UART bridge is `ttyACM0`, the console is `ttyACM1`. The apps sleep two
seconds at startup so the host can reopen the console after a reset.

`rpc_validation` and `codec_plan` link frozen generated artifacts:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rpc_validation -d build/rpc-hw -- \
  -DZEPHYR_EXTRA_MODULES=/path/to/wirelink \
  -DRPC_VALIDATION_CODEC_DIR=/path/to/frozen-codec \
  -DRPC_VALIDATION_OPTIMIZED_CODEC=1
```

`mixed_traffic` generates its codec and runtime at build time, so point it at a
matching WLC:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/mixed_traffic -d build/mixed-hw -- \
  -DZEPHYR_EXTRA_MODULES=/path/to/wirelink \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
```

The ESP32-S3 Xtensa toolchain does not support link-time optimization, so these
builds run without LTO. `codec_plan` can enable it on a host toolchain that
supports it.

Freeze the baseline ELF, map and `.config` before changing the core; build the
candidate in a separate directory. Both sides must use the same board,
toolchain and optimization setting.

## Linux USB permissions

`west flash` and `capture_serial.py` need read/write access to the board's
serial ports. Install
[`contrib/udev/99-wirelink.rules`](../../contrib/udev/99-wirelink.rules) once,
or add your user to the `uucp` group; see
[contrib/udev](../../contrib/udev/README.md).

## Capture

Start the capture, then reset the board so the boot banner and every sample are
recorded:

```sh
python benchmarks/zephyr/capture_serial.py \
  --port /dev/ttyACM0 --out after.log --until result=pass --seconds 120
```

The port is `/dev/ttyACM*` on Linux, `/dev/cu.usbmodem*` on macOS and `COM*` on
Windows. The script needs `pyserial` and reopens the port if USB re-enumerates
during reset. Without the script, `cat /dev/ttyACM0 > after.log` works on Linux;
stop it after the pass marker.

Keep the raw log, ELF, SHA-256 and `.config`. Compare firmware logs with the
matching host benchmark's `compare.py`, and never compare firmware numbers with
host numbers. `native_sim` and QEMU remain correctness gates, not timing clocks.
