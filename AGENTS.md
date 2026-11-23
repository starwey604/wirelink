# Repository Guidelines

## Repository Layout

- **Core (`src/`, `include/wirelink/`)**: Allocation-free ISO C11 core; must remain independent of OSes, drivers, heap allocation, threads, and clocks.
- **Integrations**: Platform transports in `adapters/`, host executor in `runtime/host/`, OS clock/session helpers in `runtime/platform/`, C++20 session support in `bindings/cpp/`.
- **Compiler (`wlc/`)**: Independent Rust crate generating C11 nodes/codecs and host SDKs (`wlc/src/codegen/`).
- **Examples, Docs & Tests**: Runnable tutorials in `examples/`, Zephyr apps in `samples/zephyr/`, engineering docs in `docs/`, Starlight docs site in `site/`, tests in `tests/` (`core/`, `package/`, `zephyr/`, `tutorials/`, `fixtures/`) and `wlc/tests/`.

## Build & Verification Commands

- Core C build & tests: `cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release && cmake --build build/core --parallel && ctest --test-dir build/core --output-on-failure`
- Docs sync & site checks: `python3 tests/tutorials/doc_sources.py` and `npm --prefix site run check:docs`
- WLC compiler: `cargo fmt --manifest-path wlc/Cargo.toml --check`, `cargo clippy --manifest-path wlc/Cargo.toml --all-targets --all-features -- -D warnings`, and `cargo test --manifest-path wlc/Cargo.toml`
- Zephyr suites (in a Zephyr workspace): `west twister -T tests/zephyr/unit -T tests/zephyr/integration -p unit_testing -p native_sim` (add QEMU platforms and `samples/zephyr` for full coverage)

## Architecture & Coding Discipline

- **Strict ISO C11**: Keep GCC/Clang warning-clean (`-Wall -Wextra -Wpedantic -Werror`), two-space indentation, same-line braces, `snake_case`, `wl_`/`WL_` public prefixes, SPDX headers, and deterministic generated C.
- **Flat, Unidirectional Public Model**: Keep application code on two concepts—`Transport` (`wl_transport_t` / `wl_udp_*`) ← `Node` (`<node>_bind*` + fixed `wl_node_*` lifecycle via `&self.node` + schema-specific `on_*`/`call_*`/`send_*`).
- **No Leaky Plumbing or Wrapper Sprawl**: Do not expose internal primitives (`wl_packet_port_t`, `wl_endpoint_t`, `wl_packet_stream_t`) or backend library names (e.g., `asio`) in user-facing APIs or introductory examples. Pre-1.0, refactor awkward boundaries directly rather than stacking compatibility shims or extra layers.

## CMake & Build Option Discipline

- **Minimal, Orthogonal Options**: Never add a top-level `WIRELINK_BUILD_*` option for an internal dependency (`platform`, helper libs) or an individual test/benchmark target—derive prerequisites automatically from the high-level feature that needs them.
- **Explicit Failure vs. Clear Status**: Fail loudly (`FATAL_ERROR`) when a user explicitly enables a feature whose dependency is missing; emit a clear `STATUS` message when an optional default-on example group is skipped (never silently omit targets).
- **Clean Dependency Direction**: Never let `tests/` depend on targets defined under `examples/`. Keep the C-only core build and installed `Wirelink::wirelink` package free of implicit C++ or OS dependencies.

## Runtime & Generated-Code Contract

- Keep `WIRELINK_CODEGEN_CONTRACT` synchronized between `wirelink` and `wlc`. Declare messages, RPCs, and nodes in one `.wl` (fields are required unless marked `optional` / `optional packed`); use `--node NAME` / `NODE` for single-role firmware or `--all-nodes` / `ALL_NODES` for combined targets.
- Prefer `handlers = static` in introductory examples. Synchronous TX callbacks (`<node>_bind_sync`) must accept a whole frame synchronously or reject it completely, and call `wl_node_tx_ready` once writable again.
- Preserve unconsumed stream tails (`wl_node_rx_feed`), stop external RX before closing synchronously bound nodes, and retry `wl_node_close` until `wl_node_quiescent` is true. Keep async/zero-copy physical leases alive through `wl_node_close` and driver stop acknowledgement.
- Simple RPC deadlines start on the next `wl_node_step`; `_ex` uses explicit time. Prefer `wl_node_poll` in owner loops. When calling `wl_node_step` and `wl_node_wait` separately, resample the clock between them.

## Testing, CI & Commits

- **Four Lean, Non-Overlapping Layers**: Core/node unit tests in `tests/core/` (and `tests/zephyr/unit/`), one focused boundary suite per adapter (or `tests/zephyr/integration/`), compact installed-package smoke in `tests/package/`, and compiler tests in `wlc/tests/`.
- **No Redundant Gates or Flaky Example Tests**: Extend existing suites rather than adding new CMake subprojects, wrapper harnesses, or extra CI jobs. Build-check tutorial executables under `examples/` instead of turning them into flaky network CTests, and delete superseded tests/fixtures in the same change.
- **Fixtures & Doc Sync**: Preserve compatibility baselines under `tests/fixtures/wlc/generated/` and `wlc/tests/fixtures/` unless intentionally changing the wire/ABI contract. Keep `[docs:<name>]` anchors in `examples/` synced with `docs/`, READMEs, and `site/`.
- **Commits**: Use scoped, imperative subjects (`feat:`, `fix:`, `refactor:`, `build:`, `test:`, `docs:`, `ci:`) and update `docs/`, `site/`, or `CHANGELOG.md` when public APIs, framing, or workflows change.
