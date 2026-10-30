# Composed application target test

Runs the [application upload/RPC specification](../../../examples/04_composed_services/README.md)
on Zephyr through its default Wirelink module and packet loopback. The source is
the same C application as the host example: explicit job/upload advancement,
ordinary message retry/idempotency, Query RPC, telemetry and close/drain. No USB,
flash programming, MCUboot operation or actual reboot is performed.

From an initialized Zephyr workspace with a matching local WLC 0.13.0:

```sh
west build -s /path/to/wirelink/samples/zephyr/composed_services -b native_sim \
  -d /path/to/build/composed-native -- -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
west twister -T /path/to/wirelink/samples/zephyr/composed_services \
  -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64 \
  --extra-args WIRELINK_WLC_EXECUTABLE=/path/to/wlc
```

The expected console marker is `COMPOSED_SERVICES PASS`. Select
`CONFIG_WIRELINK_PACKET_LOOPBACK` and independent CRC primitives explicitly.
Only the default C kernel is linked; there is no parallel manually compiled
legacy/prototype engine and no hidden compiler download.
