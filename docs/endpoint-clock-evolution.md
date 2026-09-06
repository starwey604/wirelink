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

Batch 1 and batch 2: implemented and locally validated. Batch 3: implementation,
bilingual docs, FFI and local measurements complete; remote CI and H7 validation
remain gates, not a release claim. WLC is pinned to
`4065b22826d00716ac8828bade79ac4fe3e764b4` (version 0.4.0, codegen ABI 21).

## Validation checkpoint — 2026-09-06

- Fresh Zephyr unit_testing/native_sim run: 29 configurations, 209 cases pass.
  Includes five endpoint-clock cases and two new RPC cache-admission cases.
- WLC: 111 tests, formatting and all-target/all-feature Clippy pass.
- Release host: 10 CTest cases pass, including UDP process fault injection,
  injected executor ownership, C++ and Python exported-C consumers.
- Clang 22 ASan/UBSan: nine native CTest cases pass; Python bridge passes with
  the ASan runtime preloaded and interpreter leak detection disabled.
- Installed package checks: core C11/C++20 2/2, split/legacy WLC 2/2, UDP 1/1.
  Core-only build/CTest 3/3. Frozen schema/pure-codec files are unchanged.
- Isolated Zephyr sample: native_sim prints `CLOCK_HIL ALL PASS`; H7 cross-build
  succeeds (62,428 bytes flash, 16,768 bytes reported RAM; GCC 14.3.0, SDK 1.0.1).
  This is not yet an H7 execution result.

The idle-gap FFI test exposed an admission-order defect: a delivered response
whose TTL had elapsed could produce CACHE_FULL before the later poll reclaimed
it. `server_begin()` now expires eligible entries during its existing lookup,
without an additional scan. Exact-TTL/wrap, TTL disabled, and protected
reserved/ready/acquired/in-flight states have regression coverage.

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

## H7 checkpoint

Windows SSH and the SEGGER probe are accessible. An initial SWD connection read
a Cortex-M7 and 1,024 KiB flash; subsequent connections failed to attach to the
CPU, including 100 kHz and automatic connect-under-reset. The user has been asked
to replug H7 and hold RESET. No test image has been flashed, no flash erased,
and no USB driver changed. Save and verify the original flash before the next
flash attempt, and restore it after isolated clock testing.

Product repositories and their long-running-test configurations remain untouched.
No main merge, tag, or release is part of this iteration.
