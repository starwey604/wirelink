<!-- SPDX-License-Identifier: Apache-2.0 -->
# Local Zephyr UART tuning patch

`esp32-uart-rx-timeout.patch` adds `CONFIG_UART_ESP32_RX_TIMEOUT` to the
Espressif UART driver. Its default is 22 character periods, matching upstream;
Kconfig settings are 1..126. The driver validates each requested UART format
against the SoC and clock dependent hardware threshold before changing format
registers or resetting FIFOs, and returns `-EINVAL` if the setting cannot fit.
ESP32-S3 allows at most 102 characters for 8N1 and 85 for 8E2; the benchmark
builder's fixed 8N1 scenario accepts only 1..102. A failed runtime format change
preserves the previous format and timeout. Zero is excluded because disabling
the timeout can leave a short frame below the RX FIFO threshold indefinitely.

Apply from a Wirelink checkout after updating Zephyr:

```sh
git -C /path/to/zephyr apply --check "$PWD/adapters/zephyr/patches/esp32-uart-rx-timeout.patch"
git -C /path/to/zephyr apply "$PWD/adapters/zephyr/patches/esp32-uart-rx-timeout.patch"
```

The patch was built and measured with Zephyr
`ca10f7fc7a5b5b1129823cfc57506e81bb109743` and hal_espressif
`b16851b3c20e6fad3d2da42ec24a3e82d0b8b444`. Check applicability again after a
Zephyr update; an upstream driver may have changed or gained its own setting.
The patch belongs to the platform driver; Wirelink framing, codecs and the
operating-system-independent core do not configure UART hardware.

The setting applies to every UART instance configured by this driver. Keep
`CONFIG_UART_ESP32_RX_FIFO_THRESH` and `CONFIG_UART_ESP32_TX_FIFO_THRESH`
unchanged when comparing timeout settings.

Run the boundary fixture against the applied patch and its matching HAL:

```sh
python3 tests/package/esp32_uart_timeout.py --zephyr-base /path/to/zephyr
```

This host fixture compiles the actual driver configure path and HAL timeout
functions with mocked peripheral I/O and the real ESP32-S3 register type. It
checks format changes, rejection without format/FIFO writes, the 102/103 and
85/86 boundaries, and settings 2 and 22. It does not measure physical UART timing.

See [the measured timeout experiment](../../../docs/uart-rx-timeout-optimization-cn.md)
for the selected opt-in configuration and its CPU/IRQ/streaming tradeoffs.

## Optional ESP32-S3 RPC linker insertion point

`esp32s3-rpc-layout.patch` exposes Zephyr's `ROM_START` input-fragment hook
inside executable Flash, before the ESP32-S3 `.text.*` catchall. It adds one
include and changes no address, padding, IRAM coverage or cache setting.

```sh
git -C /path/to/zephyr apply --check "$PWD/adapters/zephyr/patches/esp32s3-rpc-layout.patch"
git -C /path/to/zephyr apply "$PWD/adapters/zephyr/patches/esp32s3-rpc-layout.patch"
```

Then an application can call
`wirelink_wlc_group_rpc(TARGET app MODULE task LOCATION ROM_START)` after
creating its generated runtime. The module fragment has no `KEEP`: only
relocation-reachable function inputs survive GC. This patch is optional for
ordinary ON markers and unnecessary for OFF. Check the final map to confirm
that the selected inputs precede the catchall and remain independently
collectable. See [RPC layout](../../../wlc/docs/rpc-layout.md).


## DWC2 Bulk ZLP completion correction

`dwc2-bulk-zlp.patch` corrects two completion decisions in the pinned Zephyr
DWC2 driver (`ca10f7fc7a5b5b1129823cfc57506e81bb109743`): honor a requested ZLP
on Bulk IN, and finish OUT when MPS-aligned data is followed by an additional
zero-payload packet. The latter compares received packet count with data packet
count so the ZLP is not consumed and then overwritten by a receive rearm.

```sh
git -C /path/to/zephyr apply --check "$PWD/adapters/zephyr/patches/dwc2-bulk-zlp.patch"
git -C /path/to/zephyr apply "$PWD/adapters/zephyr/patches/dwc2-bulk-zlp.patch"
python3 tests/package/zephyr_dwc2_completion.py --zephyr-base /path/to/zephyr
```

The host fixture compiles the actual two driver functions with mocked register
and net-buffer I/O. It verifies 63/64/65, 127/128/129 and 511/512-byte software
completion decisions, full data plus ZLP, and both control and Bulk IN ZLP.
It models DMA/completer buffer accounting, not DMA hardware behavior; this round
only qualifies real DMA-off ESP32-S3 FS. Recheck applicability after updating
Zephyr; do not apply over an upstream fix. No Wirelink core/protocol change is
required. All four project USB benchmark groups use this same driver patch.
