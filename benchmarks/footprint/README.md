# Static footprint benchmark

[中文](README-cn.md).

This benchmark measures what Wirelink costs at build time, without running it:
the code and constant-data size of every core translation unit, and the static
RAM the library asks the caller to reserve for representative configurations.
It is a host tool with no external dependencies beyond a C compiler and a
GNU/LLVM `size`.

Wirelink allocates nothing and keeps no hidden state, so "footprint" is two
separate numbers: the loadable code/rodata the linker emits, and the caller
storage the configuration requires. The report records both.

## Run

Objects mode compiles each core source on its own with target-like flags
(`-Os -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables`) and
measures its `.text`, `.rodata`, `.data` and `.bss`:

```sh
python benchmarks/footprint/footprint.py --json footprint.json
```

Match a specific build by passing the same compiler and extra flags. Flags whose
value starts with `-` need `=`:

```sh
python benchmarks/footprint/footprint.py --cc gcc --cflags=-O2 \
  --json footprint-o2.json
```

Firmware images are measured directly from a linked ELF, which is the accurate
number because it includes dead-code elimination and cross-module inlining:

```sh
python benchmarks/footprint/footprint.py --elf build/rx/zephyr/zephyr.elf \
  --size-tool ~/zephyr-sdk/gnu/xtensa-espressif_esp32s3_zephyr-elf/bin/xtensa-espressif_esp32s3_zephyr-elf-size \
  --json footprint-esp32s3.json
```

The `size` tool must support `-A` (System V output). The tool never builds a
firmware; build the image with `west` first.

## Compare

```sh
python benchmarks/footprint/compare.py before.json after.json
python benchmarks/footprint/compare.py before.json after.json \
  --max-growth-percent 5
```

The comparison reports per-group byte deltas for the section totals, every
module or ELF section, the storage contract and the public struct sizes. A
module that appears or disappears is reported as `new`. The growth limit is an
optional gate for a dedicated runner, not a default: compiler and C-library
updates legitimately move code size by a few percent, so always compare the same
compiler, flags and mode. `--elf` and `objects` reports are not comparable to
each other.

## Report contents

| Field | Meaning |
| --- | --- |
| `totals` | Section-class totals: `text`, `rodata`, `data`, `bss`, plus `debug` (debug/unwind/comment, not loadable) and `other` |
| `modules` | Per-source size in objects mode |
| `sections` | Per-section size in ELF mode |
| `storage` | `wl_config_requirements()` output for COBS `NONE`/`CRC32C` at 20/120/2048-byte payloads, and one non-COBS config |
| `types` | Size of the public structs (`wl_ctx_t`, `wl_event_t`, `wl_config_t`, `wl_storage_t`, `wl_storage_requirements_t`) |

The storage fields are the caller's contract: `tx_payload`, `tx_unit`,
`control`, `rx_fifo` and `rx_fallback` bytes for that configuration. They are
compile-time constants, so a change here is a change to every consumer's RAM
budget and is the most important thing this benchmark tracks.

## What it does not measure

- Objects mode is pre-link. It has no cross-module inlining, no `--gc-sections`
  and no LTO, so its totals are larger than any linked image. Use `--elf` for a
  linked number.
- Object sizes depend on the compiler, version and flags, and on section naming
  such as `.text.*`. Only compare runs that used the same toolchain string,
  which the report records.
- The probe reports what the configuration requires, not what a program uses; a
  product that reserves more is not visible here.
- Host object sizes are a trend and per-module attribution signal, not the
  firmware image. For flash and RAM on a target, measure the target ELF.
- ELF mode counts every section it can attribute; allocator, bootloader and
  vendor regions are outside this tool's concern.

## Procedure

Freeze a baseline report before editing the library, record the compiler version
and flags, then rebuild the candidate and compare. Keep reports with the same
mode, compiler and flags; refresh the baseline only when the toolchain changes.
The regression note in [docs/rx-performance.md](../../docs/rx-performance.md)
asks for image and RAM size on every RX-path change; this benchmark is how that
number is produced.
