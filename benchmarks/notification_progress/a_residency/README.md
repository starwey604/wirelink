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

The subsequent `tag_mapping.py` audit corroborates this index mapping with the
published S3 ROM implementation for the configured mode. It does not establish
which of those lines were resident or fetched during a Sample.

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

## Check the ROM mapping offline before planning a tag capture

```sh
/path/to/python-with-pyelftools -B \
  benchmarks/notification_progress/a_residency/tag_mapping.py \
  --inventory /absolute/wirelink-a-residency-20261101/final/result.json \
  --rom-asm /absolute/Cache_Get_Virtual_Addr.asm \
  --out /absolute/new-mapping.json
```

The reference disassembly is retained in
`/home/ww/codings/wirelink-cache-tag-20261101/Cache_Get_Virtual_Addr.asm`.
To recreate it, obtain **`esp32s3_rev0_rom.elf`** from the official
[20260528 ROM release](https://github.com/espressif/esp-rom-elfs/releases/tag/20260528),
check the release's transfer checksum and use the matching Xtensa objdump:

```sh
xtensa-espressif_esp32s3_zephyr-elf-objdump -d \
  --disassemble=Cache_Get_Virtual_Addr esp32s3_rev0_rom.elf \
  > Cache_Get_Virtual_Addr.asm
```

Follow the vendor's revision-selection rule; the measured S3 is revision0.2,
for which this release provides rev0. Do not select the similarly named S31 ELF.
The audit interprets only this one helper's integer/load/branch instructions,
using post-ENTRY arguments. It checks all 64 groups at offset endpoints for four
address tags (512 cases), then reclassifies the prior candidate lines.
It never executes code on the target. The legacy tag aperture formula comes
separately from the eight-way, one-bank traversal path.

`locate`, `legacy_tag_address` and `decode_tag` expose the corresponding offline
mapping and SDK raw-word fields. An invalid raw tag has no resident line address;
one invalid way does not establish a miss. `fifo_field` is a field name, not a
verified replacement-policy model. The decoder has only synthetic raw-word
tests so far: no target tag snapshot has been captured.

Both ROM tag traversal APIs suspend cache, and the second also writes tag-query
selector registers. Do not invoke them through a debugger and call the result
an undisturbed live timing measurement. This tool produces no target read/write
commands. See the [connection and ROM audit](../../../docs/static-endpoint-cache-tag-access-20261101-cn.md)
for the actual capture dependency and register-interface evidence.

```sh
/path/to/python-with-pyelftools -B -m unittest discover \
  -s benchmarks/notification_progress/a_residency -p 'test_tag_mapping.py'
```

## Separate Progress work, successful traffic and owner cost offline

`inspect_stream.py` reads the required-fixed candidate's retained disassembly,
symbol tables and raw A/B screen. It needs neither candidate ELF files nor a
board connection:

```sh
/path/to/python-with-pyelftools -B \
  benchmarks/notification_progress/a_residency/inspect_stream.py \
  --evidence /absolute/wirelink-required-fixed-20261101 \
  --baseline /absolute/wirelink-cpu-optimization-20261101 \
  --a-disassembly /absolute/wirelink-a-residency-20261101/final \
  --out /absolute/new-stream-inspection.json
```

The tool reuses the raw screen/qualification/state replay. It compares function
operations and branch destinations separately from literal referents and layout.
Padding is excluded from business instruction counts; internal branches retain
their destination instruction ordinal. Integer literals use their value, not
objdump's arbitrary nearest-symbol annotation. A matching operation/CFG model
does not establish matching data references or dynamic execution frequency.

For this frozen 32 B/5 s screen, completed USB byte ledgers also determine
successful ordinary Progress counts: subtract the three control exchanges,
43-byte data frames and one 23-byte DRAIN/END marker in each direction; remaining
bytes must divide exactly into 23-byte Progress frames. Control sizes include
the actual tagged-varuint fields and raw JSON spelling. These counts cover
preparation, window and drain together, not just the CPU window. Failed send
attempts, empty service passes and wakes remain unknown.

CPU components are divided by concurrent goodput to obtain cost per equivalent
payload frame. Inclusive owner/thread/IRQ spans overlap and must not be summed
or subtracted into exclusive costs. All 36 stream observations are retained;
state-ineligible pairs stay marked and are not promoted to acceptance evidence.

The [inspection result](../../../docs/static-endpoint-required-fixed-stream-inspection-20261101-cn.md)
records the concrete findings and remaining frequency-versus-execution-cost
question. Focused helper tests run with `-p test_stream.py` in this directory.
