# Repository Guidelines

## Project Structure & Module Organization

The allocation-free ISO C11 core lives in `src/`, with public headers in `include/wirelink/`; keep it independent of operating systems, drivers, heap allocation, threads, and clocks. Platform integrations belong in `adapters/`; host execution wrappers are in `runtime/host/`, C++20 UDP client support is in `bindings/cpp/`, and the Rust schema compiler lives in `wlc/` (code generation engine under `wlc/src/codegen/`). Examples, Zephyr samples, benchmarks, and fuzz targets use matching top-level directories (`examples/`, `samples/zephyr/`, `benchmarks/`, `fuzz/`). Tests are split among host unit tests in `tests/core/` and `tests/packet_tools/`, Ztest suites in `tests/zephyr/` (`unit/` and `integration/`), package checks in `tests/package/`, tutorial snippet sync checks in `tests/tutorials/doc_sources.py`, Rust tests in `wlc/tests/`, and compatibility fixtures in `tests/fixtures/`. Simplified Chinese engineering docs live in `docs/` (`README.md`, `wire-format.md`, `porting-adapters.md`, `development.md`).

## Build, Test, and Development Commands

- `cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release` configures the core, loopback example, and host checks.
- `cmake --build build/core --parallel` builds the configured C targets.
- `ctest --test-dir build/core --output-on-failure` runs host core tests, packet-tool tests, loopback smoke, and `wirelink_tutorial_doc_sources`.
- `cargo test --manifest-path wlc/Cargo.toml` runs the WLC compiler suite.
- `cargo clippy --manifest-path wlc/Cargo.toml --all-targets --all-features -- -D warnings` enforces Rust lint cleanliness.
- `west twister -T tests/zephyr/unit -T tests/zephyr/integration -p unit_testing -p native_sim` runs Zephyr C suites from an initialized Zephyr workspace. Add QEMU platforms (`qemu_cortex_m3`, `qemu_riscv32`, `qemu_x86_64`) and `samples/zephyr` for full coverage.

## Coding Style & Naming Conventions

Use strict ISO C11 and keep GCC/Clang builds warning-clean (`-Wall -Wextra -Wpedantic -Werror`). Follow two-space indentation, same-line braces, and `snake_case` filenames/functions. Public symbols use `wl_`; macros and enum constants use `WL_`. Retain SPDX headers. Format Rust with `cargo fmt --manifest-path wlc/Cargo.toml` and keep generated C deterministic.

## Testing Guidelines

Add pure-core tests in `tests/core/` or Ztest `type: unit` scenarios under `tests/zephyr/unit/`; use `tests/zephyr/integration/` for transport, emulator, or adapter behavior. Preserve compatibility fixtures under `tests/fixtures/wlc/generated/` and `wlc/tests/fixtures/` unless an intentional wire or ABI contract change requires updating them. Add focused `wlc/tests/` cases for compiler changes, and keep `[docs:<name>]` anchors in `examples/` synced with Markdown blocks checked by `tests/tutorials/doc_sources.py`.

## Commit & Pull Request Guidelines

Recent commits use concise imperative subjects with prefixes such as `feat:`, `fix:`, `build:`, `test:`, and `ci:`. Keep each commit scoped. Pull requests should explain behavior and compatibility impact, link issues, list commands/platforms tested, and update `docs/` or `CHANGELOG.md` when APIs, Wire v2 framing, generated output, or workflows change. Include logs or `benchmarks/footprint/footprint.py` results for performance or footprint work.
