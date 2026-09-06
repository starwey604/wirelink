# Endpoint clock evolution

Work stays on development branches. No release or main merge. Hardware checks
are now authorized on Windows `moonf@192.168.8.5` with H7/J-Link, after simulation
gates. Preserve existing product branches and avoid driver changes.

## Batch 1 — time contract and core

- Add an owner-side C clock descriptor, copied at endpoint initialization.
- Require a clock, read once per bounded pass, reuse it in nested replies.
- Make advanced reliable submission/claim commit take explicit time.
- Prime the pump time before adapter completion service; never poll just to
  refresh a business submission's time.
- Verify first submission, idle gaps, async completion, wrap, isolation,
  lifecycle, and clock-read budgets with a manual clock.

## Batch 2 — generated API and integrations

- WLC ABI 21: configure a clock once; remove now from default endpoint calls,
  replies, rejection, step, and hints. Keep explicit-time advanced runtimes.
- Migrate host examples/executor and fixtures without changing frozen schemas
  or protocol bytes. Use one source for transport/runtime deadlines.
- Run compiler, native, and UDP regression suites.

## Batch 3 — bindings, performance, documentation

- Validate exported C shims from C++ and Python using native/manual clocks;
  do not add a full Python SDK or Python callbacks to the normal hot path.
- Measure clock reads and host costs, including no-clock unreliable sends.
- Update bilingual tutorials, migration contracts, package/CI pins.
- Run sanitizer, simulation, package and cross-platform host checks. Validate
  the isolated Zephyr clock sample on the authorized H7, without product changes.

## Status

All three batches are implemented and validated: bilingual docs, FFI, local
measurements, remote CI, and isolated H7 execution are complete. This is not a
release or a physical-transport/product-firmware validation claim. WLC is pinned to
`b5c444ab09bbbc1490e1e69307a760342a1633d0` (version 0.4.0, codegen ABI 21).

## Validation checkpoint — 2026-09-06

- Fresh Zephyr unit_testing/native_sim run: 29 configurations, 209 cases pass.
  Includes five endpoint-clock cases and two new RPC cache-admission cases.
- Additional Cortex-M3/RISC-V32/x86_64 QEMU integration: 15 configurations,
  72 cases pass (three unsupported configurations statically filtered).
- WLC: 111 tests, formatting and all-target/all-feature Clippy pass.
- Release host: 10 CTest cases pass, including UDP process fault injection,
  injected executor ownership, C++ and Python exported-C consumers.
- Clang 22 ASan/UBSan: nine native CTest cases pass; Python bridge passes with
  the ASan runtime preloaded and interpreter leak detection disabled.
- Installed package checks: core C11/C++20 2/2, split/legacy WLC 2/2, UDP 1/1.
  Core-only build/CTest 3/3. Frozen schema/pure-codec files are unchanged.
- Astrial serial/USB host build and virtual-serial CTest: 4/4 pass.
- Isolated Zephyr sample: native_sim prints `CLOCK_HIL ALL PASS`; H7 cross-build
  succeeds (62,428 bytes flash, 16,768 bytes reported RAM; GCC 14.3.0, SDK 1.0.1).
  Real H7 execution also passes; see the hardware checkpoint below.

The idle-gap FFI test exposed an admission-order defect: a delivered response
whose TTL had elapsed could produce CACHE_FULL before the later poll reclaimed
it. `server_begin()` now expires eligible entries during its existing lookup,
without an additional scan. Exact-TTL/wrap, TTL disabled, and protected
reserved/ready/acquired/in-flight states have regression coverage.

The first remote Host CI passed Windows/Linux but Apple Clang rejected a
generated constant delivery self-comparison. WLC now selects clock-read code
during generation, emitting no clock access for unreliable sends. Compiler
regression checks and a Clang strict-warning installed consumer pass; matched
pins and fixtures were regenerated for the cross-platform rerun.

The corrected Wirelink revision `0a8fa0a` passed [Host CI](https://github.com/starwey604/wirelink/actions/runs/34031263969)
and [Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34031263977).
The matching WLC revision passed [compiler CI](https://github.com/starwey604/wlc/actions/runs/34031263727).

### Automated sample regression

The post-H7 audit found that `samples/zephyr/endpoint_clock` had only been run
manually. It now has a Twister console scenario and is included in Zephyr CI,
which builds the same pinned standalone WLC used by Host CI before generating
the sample's codec and runtime. No local `wlc/` worktree or unpublished release
download is required by that CI job.

The focused local run passed all four executed configurations on native_sim,
Cortex-M3 QEMU, RISC-V32 QEMU, and x86_64 QEMU, with no filtered configurations
or warnings. The harness requires startup, all four RPC outcomes, the one-read
idle-pass result, and final success in order. Simulated cycle counts are not
performance pass/fail criteria. See the sample README for the reproducible
Twister command. This follow-up only adds regression metadata, CI and docs;
the H7-tested application, core, generated APIs and compiler are unchanged.

## Host cost observation

Linux x86_64, Intel Core 5 315, GCC 16.2.1 Release. Three runs of two million
operations each, 1,000 warmups, no CPU affinity/frequency isolation. These are
observations on a development machine, not performance guarantees or firmware
latency results. CPU time includes the test's clock-read counter instrumentation.

| Path | Clock reads/op | Wall ns/op range | CPU ns/op range |
| --- | --- | --- | --- |
| Explicit-time idle pump | 0 | 12.93–18.16 | 6.75–17.95 |
| Default endpoint, manual clock | 1 | 8.43–22.44 | 8.29–21.80 |
| Default endpoint, native clock | 1 | 25.24–42.99 | 24.93–42.34 |
| Unreliable typed submit | 0 | 39.04–58.72 | 38.66–57.62 |

The paths have different assembly work and measurement noise; do not subtract
them to claim an isolated callback cost. Host `wl_endpoint_t` is 1,024 bytes;
the generated telemetry endpoint is 1,440 bytes. These are total sizes, not growth.

## H7 checkpoint — 2026-09-06

Windows SSH and SEGGER probe 609799419 are accessible (Commander/DLL 9.72,
probe firmware dated 2021-05-07). After replug, SWD at 100 kHz reads a Cortex-M7,
DBGMCU IDCODE `0x10016483`, and flash-size value `0x0400` (1,024 KiB).
Early subsequent attachment attempts failed, including automatic connect-under-reset.

A fresh direct backup attempt reached `savebin` for `0x08000000`, length
`0x00100000`, but then failed with `Communication timed out: Requested 8196
bytes, received 0 bytes`. The resulting `original-flash.bin` is **zero bytes**,
not a valid backup and never suitable for restoration. Windows logs are retained
under `C:\Users\moonf\codings\wirelink-clock-hil-20260906-abi21`.
The cause of that timeout is not established. The user subsequently confirmed
RESET was released, identified the probe as a clone, and explicitly authorized
overwriting the old firmware without a backup.

Direct flashing then succeeded without another manual replug or USB restart.
The session attached at 100 kHz, switched SWD to 1 MHz, reset/halted, and used
`loadfile` to program and verify the ELF. J-Link reported one affected 128 KiB
range at `0x08000000` and a 2.337 s download. The old contents of that range were
overwritten; there is no valid backup. No full-chip erase, option-byte change,
driver replacement, or external-storage operation was performed.

Test artifact: Wirelink `db7cfcb` (software matches CI-tested `0a8fa0a`), WLC
`b5c444a`, Zephyr `v4.4.0-11610-gbd8c15382376`, board
`dm_mc02/stm32h723xx`, Cortex-M7 at configured 550 MHz, speed optimization,
I/D caches enabled. ELF SHA-256:
`3e239fec9fe17319692350f6819c6da520576013d8b837c40995dccbc3161479`.

After reset/run and 1.5 seconds, the same debug connection halted the CPU and
saved 4,352 bytes of RTT RAM at `0x24000000`, then resumed execution. The RTT
control block was at `0x24001010`; channel 0 used a 4,096-byte ring at
`0x24000010` with write offset 464 and read offset zero. Reading that ring gave:

```text
CLOCK_HIL ABI=21 native_ms=0 start
CLOCK_HIL rpc mode=0 idle_ms=75 elapsed_ms=1 client_units=2 PASS
CLOCK_HIL rpc mode=0 idle_ms=150 elapsed_ms=1 client_units=2 PASS
CLOCK_HIL rpc mode=1 idle_ms=75 elapsed_ms=1 client_units=2 PASS
CLOCK_HIL rpc mode=2 idle_ms=75 elapsed_ms=50 client_units=1 PASS
CLOCK_HIL idle iterations=20000 cycles/op=1060 ns/op=1928 reads/op=1 endpoint_bytes=2000
CLOCK_HIL ALL PASS
```

This validates real-clock idle-gap submission, inline completion/rejection,
50 ms timeout, clock-read budgets, and absence of premature retransmission on
the H7. Client and server share the same CPU and in-memory loopback; the 1 ms
RPC observation includes the test's 1 ms cooperative sleep, not physical-link
latency. Idle passes average about 1.93 microseconds, including enabled IRQs,
timer work and clock-count instrumentation. This is one current-build observation,
not an isolated CPU-time measurement or a before/after speedup claim. The
2,000-byte endpoint size is the total generated calculator endpoint size.

Raw artifacts `flash.log` and `rtt-ram.bin` are retained in the Windows directory
above, with local copies under ignored `build/`.

The new [Windows recovery helper](../samples/zephyr/endpoint_clock/restart-jlink.ps1)
was tested with `-WhatIf`, rejection of a non-J-Link instance, and an actual
`pnputil /restart-device` of `USB\VID_1366&PID_0101\000609799419`.
Windows reported success and the device returned `OK`, but the subsequent
Commander connection again failed in `InitTarget()` after initializing the DAP.
Consequently, device-binding restart is available but **has not solved this
probe's attachment failure**; it is not a substitute for physical power cycling.
The unsuccessful rerun did not reflash the board and does not invalidate the
captured passing run. The test image remains installed; no original image was
restored. Request manual replug/reset assistance only when the next hardware
operation actually needs it.

Product repositories and their long-running-test configurations remain untouched.
No main merge, tag, or release is part of this iteration.
