# Composed service target test

Runs the [host executable specification](../../../examples/04_composed_services/README.md)
unchanged on Zephyr. Two endpoints communicate through in-memory loopback;
this does not validate USB Bulk, actual flash programming or MCUboot swaps.
Expected console marker: `COMPOSED_SERVICES PASS`.

## Build and run

From an initialized Zephyr workspace with a matching WLC compiler:

```sh
west build -s /path/to/wirelink/samples/zephyr/composed_services -b native_sim \
  -d /path/to/build/composed-native -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
```

Twister runs this on `native_sim` and QEMU.

## ESP32-S3 hardware run

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/composed_services -d build/composed-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
west flash -d build/composed-hw
python benchmarks/zephyr/capture_serial.py \
  --port /dev/ttyACM0 --out composed.log --until "COMPOSED_SERVICES PASS" --seconds 120
```

The image is inert: it never reboots, operates actuators or writes flash.
