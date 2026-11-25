<!-- SPDX-License-Identifier: Apache-2.0 -->
# Local Zephyr UART tuning notes

Upstream Zephyr `main` (commit `3330056ad76`) added native per-port
`rx-timeout`, `rx-fifo-threshold`, and `tx-fifo-threshold` devicetree properties
to `espressif,esp32-uart`, superseding the former local
`esp32-uart-rx-timeout.patch`. Select per-port RX timeout directly in board or
application devicetree overlays while keeping FIFO thresholds unchanged when
comparing settings.

## Temporary ESP32 UART patches (`main` baseline)

While tracking Zephyr `main`, two upstream ESP32 UART fixes are carried here and
applied by `.github/workflows/zephyr.yml` until merged upstream:

- `esp32-uart-err-check.patch` (upstream PR `#127119`): reads `INT_RAW` error
  bits in `uart_esp32_err_check()` so disabled-IRQ polling and subsequent
  `uart_irq_err_enable()` do not miss or replay stale line errors.
- `esp32-uart-async-perf.patch` (upstream PR `#127125`): tightens async RX
  buffer handoff and UHCI threshold order at EOF, serializes `uart_rx_buf_rsp()`,
  and reuses TX GDMA configuration via `dma_reload()`.

```sh
git -C /path/to/zephyr apply --check "$PWD/adapters/zephyr/patches/esp32-uart-err-check.patch"
git -C /path/to/zephyr apply "$PWD/adapters/zephyr/patches/esp32-uart-err-check.patch"
git -C /path/to/zephyr apply --check "$PWD/adapters/zephyr/patches/esp32-uart-async-perf.patch"
git -C /path/to/zephyr apply "$PWD/adapters/zephyr/patches/esp32-uart-async-perf.patch"
```

## Optional ESP32-S3 RPC linker insertion point

`esp32s3-rpc-layout.patch` exposes Zephyr's `ROM_START` input-fragment hook
inside executable Flash, before the ESP32-S3 `.text.*` catchall. It adds one
include and changes no address, padding, IRAM coverage or cache setting.

```sh
git -C /path/to/zephyr apply --check "$PWD/adapters/zephyr/patches/esp32s3-rpc-layout.patch"
git -C /path/to/zephyr apply "$PWD/adapters/zephyr/patches/esp32s3-rpc-layout.patch"
```

This hook is retained for archived linker-layout experiments. Current WLC 0.13
removed the `wirelink_wlc_group_rpc` runtime/layout helper; the new kernel does
not install old endpoint IRAM fragments or use this hook as a speed guarantee.
The patch changes no section address/padding by itself and remains part of the
explicit platform source identity used by the recorded comparisons. Check the
final ELF/map and generated source for any new placement experiment.


## DWC2 Bulk ZLP completion correction

`dwc2-bulk-zlp.patch` corrects two completion decisions in the pinned Zephyr
DWC2 driver (`ca10f7fc7a5b5b1129823cfc57506e81bb109743`): honor a requested ZLP
on Bulk IN, and finish OUT when MPS-aligned data is followed by an additional
zero-payload packet. The latter compares received packet count with data packet
count so the ZLP is not consumed and then overwritten by a receive rearm.

```sh
git -C /path/to/zephyr apply --check "$PWD/adapters/zephyr/patches/dwc2-bulk-zlp.patch"
git -C /path/to/zephyr apply "$PWD/adapters/zephyr/patches/dwc2-bulk-zlp.patch"
python3 tests/zephyr/host_fixtures/zephyr_dwc2_completion.py --zephyr-base /path/to/zephyr
```

The host fixture compiles the actual two driver functions with mocked register
and net-buffer I/O. It verifies 63/64/65, 127/128/129 and 511/512-byte software
completion decisions, full data plus ZLP, and both control and Bulk IN ZLP.
It models DMA/completer buffer accounting, not DMA hardware behavior; this round
only qualifies real DMA-off ESP32-S3 FS. Recheck applicability after updating
Zephyr; do not apply over an upstream fix. No Wirelink core/protocol change is
required. All four project USB benchmark groups use this same driver patch.
