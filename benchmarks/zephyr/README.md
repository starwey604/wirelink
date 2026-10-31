# Current Zephyr firmware diagnostics

[中文](README-cn.md).

These apps use the same C workload as their host counterparts. Native/QEMU
runs check correctness; firmware cycles on an identified board are separate
intrinsic diagnostics, not USB latency or whole-system active CPU.

| App | Current format / reader |
| --- | --- |
| `codec_plan` | `codec_plan_*_v1`; `codec_plan/compare.py`; frozen pure codec directory |
| `framing` | `framing_cpu_*_v3`; `framing/compare.py`; independent framing tools |
| `mixed_traffic` | `mixed_kernel_fw_*_v1`, 42 traffic/21 cycle groups; `mixed_traffic/compare_kernel_firmware.py` |
| `rpc_validation` | `rpc_validation_*_v3`; `rpc_validation/compare.py`; public codec operations |

The old RX backend selector experiment is retired, with its file hashes and
replacements in `../retired-execution-workloads.csv`. Historical readers do
not make removed executables current workloads. Never pool old ARQ/replay
captures with these formats.

From an initialized matching Zephyr workspace, build into a fresh directory:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/framing -d build/framing-hw
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rpc_validation -d build/codec-validation-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/mixed_traffic -d build/mixed-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
```

`codec_plan` requires `CODEC_PLAN_CODEC_DIR` containing separately generated
public `plan.c/.h/_values.h`. Preserve frozen baseline artifacts, generate with
the intended compiler into another directory, and record both identities.
Current public codec validation no longer accepts the private optimized
fingerprint seam. Do not set `RPC_VALIDATION_OPTIMIZED_CODEC=1`.

The ESP32-S3 apps select the USB Serial/JTAG console and wait two seconds before
printing. Discover the actual board/flash bridge/console paths; device names
are not stable. Flash only the selected workload intentionally, keep the
flash bridge and console ownership separate and close both processes afterward.
An unrelated diagnostic image does not satisfy the static-kernel plan's final
good-B handoff.

`capture_serial.py` creates a new output exclusively and fails if its expected
end marker is absent:

```sh
python benchmarks/zephyr/capture_serial.py --port DISCOVERED_CONSOLE \
  --out complete.log --until result=pass --seconds 120
```

Preserve source/toolchain/generated C, observer/LTO/cache settings, ELF/bin/map,
config/commands, all raw records and failed attempts. Use the reader for the
actual format and require its complete matrix. IRQ-masked mixed-model cycles
include copies, fault injection and application bookkeeping; simulated ms stay
separate. Neither these diagnostics nor a board build qualify physical I/O.
