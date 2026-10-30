# Static footprint diagnostics

[中文](README-cn.md).

Current `wirelink-footprint-v2` compiles every `src/*.c` separately and records
object sections, actual compiler version/flags and source hashes. This sum
includes optional independent framing/container/stream tools: it is **not** the
linked default product image size. The default native kernel does not acquire
those optional execution costs merely because their objects exist.

```sh
python benchmarks/footprint/footprint.py --json footprint.json
python benchmarks/footprint/footprint.py --cc clang --json clang.json
python benchmarks/footprint/compare.py before.json after.json
```

The C probe uses the checked-in generated demo kernel's actual layout, plus
packet port, stream, byte FIFO and call-handle sizes. Three explicit example
assemblies show endpoint, port, two RX units, optional stream TX/object and
optional byte staging budgets. Every storage number is bytes. Generated endpoint
size already includes kernel metadata, four request snapshots, four reply
snapshots, one ordinary snapshot and codec scratch. Do not add these twice.
Native USB reserves one byte beyond its maximum frame. Stream budgets use the
actual selected connector bound. Byte staging uses a caller-selected sample
budget, not a universal library requirement. Driver objects and padding in an
application's enclosing structure are excluded.

The standalone probe does not compile the demo codec into the source-object sum.
Actual linked-image text/rodata/RAM and whole-program layout require the final
ELF/map/config and compile/link commands. `--elf IMAGE --size-tool TOOL` records
sections and the ELF hash only; it explicitly lacks the original build identity
and is inspection data, not an automatic comparable/gated pair. The hardware
refactor report records actual qualified images and resource ledgers separately.

Historical v1 reports remain readable. The comparator rejects cross-schema,
mode and compiler mismatches and incomplete/negative v2 storage rows. Structure
and representative storage changes are exact-contract review signals; section
byte growth can use an optional gate. Keep raw source, tool and image provenance
alongside every report. No wall-time or CPU improvement is inferred from size.
