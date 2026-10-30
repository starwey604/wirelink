# Repository Guidelines

## Project Structure & Module Organization

The allocation-free C11 core lives in `src/`, with public headers in `include/wirelink/`; keep it independent of operating systems, drivers, heap allocation, threads, and clocks. Platform integrations belong in `adapters/`; the desktop executor is in `runtime/host/`, and the Rust schema compiler is in `wlc/`. Examples, Zephyr samples, benchmarks, and fuzz targets use matching top-level directories. Tests are split among Ztest suites in `tests/zephyr/`, package checks in `tests/package/`, Rust tests in `wlc/tests/`, and fixtures in `tests/fixtures/`. Protocol and ownership contracts live in `docs/`.

## Build, Test, and Development Commands

- `cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release` configures the core and examples.
- `cmake --build build/core --parallel` builds the configured C targets.
- `cargo test --manifest-path wlc/Cargo.toml` runs the WLC compiler suite.
- `cargo clippy --manifest-path wlc/Cargo.toml --all-targets --all-features -- -D warnings` enforces Rust lint cleanliness.
- `west twister -T tests/zephyr/unit -T tests/zephyr/integration -p unit_testing -p native_sim` runs primary C suites from an initialized Zephyr workspace. Add the `README.md` QEMU platforms for full coverage.
- `ctest --test-dir <build-dir> --output-on-failure` runs host-only tests when the selected CMake options enable them.

## Coding Style & Naming Conventions

Use strict ISO C11 and keep GCC/Clang builds warning-clean (`-Wall -Wextra -Wpedantic`). Follow two-space indentation, same-line braces, and `snake_case` filenames/functions. Public symbols use `wl_`; macros and enum constants use `WL_`. Retain SPDX headers. Format Rust with `cargo fmt --manifest-path wlc/Cargo.toml` and keep generated C deterministic.

## Testing Guidelines

Add pure-core tests as Ztest `type: unit` scenarios; use integration scenarios for transport, emulator, or adapter behavior. Name directories by capability, such as `tests/zephyr/unit/bulk_sender`. Preserve frozen-v1 conformance vectors when changing framing or codecs. Add focused `wlc/tests/` cases for compiler changes. There is no numeric coverage threshold; relevant suites and supported platforms must pass.

## Commit & Pull Request Guidelines

Recent commits use concise imperative subjects with prefixes such as `feat:`, `fix:`, `build:`, `test:`, and `ci:`. Keep each commit scoped. Pull requests should explain behavior and compatibility impact, link issues, list commands/platforms tested, and update docs, fixtures, or `CHANGELOG.md` when APIs, wire behavior, generated output, or workflows change. Include logs or benchmark results for performance work.
