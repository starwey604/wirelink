# Inspect the accepted A images without rebuilding them

Use this tool to locate the actual Sample calls, literal loads and instruction
line candidates in the retained A-off/on ELF files. It never builds, flashes,
connects to a board or changes cache settings.

```sh
/path/to/python-with-pyelftools -B \
  benchmarks/notification_progress/a_residency/analyze.py \
  --baseline /absolute/wirelink-cpu-optimization-20261101 \
  --objdump /absolute/xtensa-espressif_esp32s3_zephyr-elf-objdump \
  --out /absolute/new-inventory
```

`--baseline` contains `A-off` and `A-on`, each with `build.json`, the original
`device/zephyr/zephyr.elf`, and `artifacts/device.config`. The tool binds the ELF
to its existing measurement receipt, checks the configured geometry and refuses
an existing output directory. It needs pyelftools and the matching Xtensa
objdump, already available in the research environment. Output is text
(`A-off.asm`, `A-on.asm`, `result.json`), not copied images or object files.

## Start with the successful Sample path

The 11 business functions are explicit in `API`. `SUCCESS_BRANCHES` annotates
conditional outcomes reviewed against the A assembly and frozen sources:
Sample kind, all four fixed32 fields present, valid pointers and capacity,
free ordinary snapshot, successful encoding/sealing. The walker follows direct
jumps, rejects missing branch annotations, loops and padding, and counts four
calls to `wlc_put32`. This is static path reconstruction, not an emulator or a
measurement of retired instructions. IRQs, register-window exception execution
and timing are outside that reconstruction.

The `main-clock-bracket` contains the original three calls: first clock, send,
last clock, with the first clock's L32R included as a conservative boundary.
Indirect calls resolve automatically only when the immediately preceding
instruction loads that same register via L32R. Others stay explicitly unresolved.
The Sample encoder callback binding is separately recorded from its literal and
reviewed ABI argument moves. Duplicate local names require a caller-qualified
name, such as `atomic_set_bit@wl_kernel_usb_bulk_service`; the tool does not pick
an arbitrary local symbol.

`code_comparisons` normalizes relocated direct call/branch targets and L32R
values referring to named symbols. It retains registers, opcodes, relative
branch destinations, other immediates and unnamed literal values. Equality means
that normalized instruction structure matches, not that linked bytes or runtime
state are identical. Whole `main` intentionally differs in startup identity
printing; inspect the separate timing bracket. The six window vectors are
compared directly, without relocation normalization.

## Treat line inventories as candidates, not cache simulations

The configured line size is 32 B. Every instruction's complete byte range is
covered, including an instruction crossing a line boundary. L32R **slots** in
IROM are included; their referents may instead be internal data, a function or
a numeric constant. Do not count a DRAM referent as another ICache line or
assume every IBUS miss is an instruction fetch.

- `api_success` contains the annotated successful path plus the clock bracket.
- `api_envelope` covers whole bodies, including cold/error branches, of those
  same business functions plus the bracket.
- Owner progression and USB dispatch groups are explicit, curated body/literal
  inventories. They omit some callees and include unexecuted branches. They
  are neither a complete dynamic trace nor a proven upper bound on all traffic.
- Periodic observer hooks are separated from boundary-only snapshot/export
  functions. Off-mode observer groups are deliberately empty, not an assertion
  that all off-mode export stubs were linked out.

Geometry gives `16384/(8*32) = 64` sets. Candidate grouping **assumes conventional
low-bit indexing**, `set = (VA//32)%64`, with a 2048 B same-set period. The consulted
ESP32-S3 TRM does not specify index-bit selection or replacement policy. Thus
`conventional_set` and `distinct_collected_lines > 8` screen possible conflicts;
they do not establish actual residency, compulsory eviction, miss count or cost.
Whole-function envelopes also do not establish that their lines are accessed
between two Samples. Same-line references are deduplicated and distinguished
from different-tag candidates.

```sh
/path/to/python-with-pyelftools -B -m unittest discover \
  -s benchmarks/notification_progress/a_residency -p 'test_*.py'
```

Tests cover boundary-straddling instructions, line deduplication and memory
classes, annotated branch walking/rejection, literal-slot versus referent
identity, bounded indirect-call resolution and duplicate local symbols.
The actual A inventory is in
`/home/ww/codings/wirelink-a-residency-20261101/final/`; the interpretation is in
`docs/static-endpoint-a-residency-analysis-20261101-cn.md`.
