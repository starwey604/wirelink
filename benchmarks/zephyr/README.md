# Zephyr firmware benchmarks

[中文](README-cn.md).

These apps run the `benchmarks/` workloads on the ESP32-S3 DevKitC and report
cycles or wall time over the board's USB Serial/JTAG console. No J-Link or RTT
probe is required. The reference board is
`esp32s3_devkitc/esp32s3/procpu`.

| App | Host counterpart |
| --- | --- |
| `codec_plan` | [codec_plan](../codec_plan/README.md) |
| `framing` | [framing](../framing/README.md) |
| `mixed_traffic` | [mixed_traffic](../mixed_traffic/README.md) |
| `rpc_validation` | [rpc_validation](../rpc_validation/README.md) |
| `rx_backend` | Zephyr-only RX ring test |

Each app selects `zephyr,console = &usb_serial` in its `esp32s3_devkitc.overlay`,
so the firmware output and the host `compare.py` logs use the same text format.

## Build and flash

From an initialized Zephyr workspace, for example `framing`:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/framing -d build/framing-hw
west flash -d build/framing-hw
```

`rpc_validation` and `codec_plan` link frozen generated artifacts:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rpc_validation -d build/rpc-hw -- \
  -DRPC_VALIDATION_CODEC_DIR=/path/to/frozen-codec \
  -DRPC_VALIDATION_OPTIMIZED_CODEC=1
```

Freeze the baseline ELF, map and `.config` before changing the core; build the
candidate in a separate directory. Both sides must use the same board, toolchain
and LTO setting.

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
