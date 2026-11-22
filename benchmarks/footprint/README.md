# Static footprint diagnostics

[中文](README-cn.md).

Current `wirelink-footprint-v2` compiles every `src/*.c` separately and records
object sections and actual compiler version/flags. Source/image integrity hashes
are enabled by default; use `--no-digests` for ordinary size comparisons. This sum
includes optional independent framing/container/stream tools: it is **not** the
linked default product image size. The default native kernel does not acquire
those optional execution costs merely because their objects exist.

```sh
python benchmarks/footprint/footprint.py --no-digests --json footprint.json
python benchmarks/footprint/footprint.py --no-digests --cc clang --json clang.json
python benchmarks/footprint/compare.py before.json after.json
```

The C probe uses the checked-in generated demo node's actual layout, plus
packet port, stream, byte FIFO and call-handle sizes. Three representative assemblies split the node's bytes into port, RX slots,
stream object, TX wire buffer and remaining node storage (including ABI
padding). The native total uses `sizeof(demo_node_t)`. The stream layouts use
the generated prefix and the selected connector's bounds, including the
extra byte per RX slot and the ABI alignment of each trailing member.
The COBS assembly also includes a caller-selected FIFO and staging buffer.
Driver objects and padding in an
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
