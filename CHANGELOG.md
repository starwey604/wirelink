# Release notes

## 0.14.0

- Replace binding profiles with single-file RPC and node declarations, explicit
  multi-node selection, and one generated C source/header pair.
- Make fields required by default and remove required-field presence members;
  retain presence only for optional fields and optional packed arrays.
- Generate static handlers and embedded packet/stream I/O by default, with
  explicit dynamic handlers and external asynchronous ports when needed.
- Size node TX/RX storage by role; expose consumed-byte and TX-ready backpressure
  contracts, deferred simple-call timeouts and explicit-time extended RPC calls.
- Qualify embeddable C/C++ packet-port layouts and use exact codegen contract
  0.14. Regenerate all SDKs and migrate installed, host and Zephyr consumers.

See [the migration notes](docs/release-v0.14.0-cn.md).

## 0.13.0

- Freeze the compiler source pair in `.github/release-pair.json`; add release
  rehearsals, complete SDK inventory/provenance checks and promotion of the
  original tested bundles after the matching compiler is published.

- Export C11 and repair MSVC atomics propagation for mixed C/C++ and installed consumers;
  regenerate codecs with portable descriptor definitions and ZigZag masks.
- Wait for observable UDP receipt in adapter tests, normalize footprint paths
  on Windows, and separate benchmark Python 3.11+ tooling from SDK Python 3.10
  runtime coverage.

- Allocation-free C11 endpoint kernel with static typed routing.
- Four concurrent outbound RPC calls, four reply snapshots and one ordinary
  message snapshot, scheduled through a nine-entry admission queue.
- Owner-supplied monotonic time, bounded work budgets and one terminal callback
  per admitted call.
- Packet ports with two RX units, token-validated completion and distinct
  source-consumed and physically-drained TX boundaries.
- Profile-v2 generation with WLC codegen 0.13 and SDK binding API 3.
- Native/NONE, COBS/CRC16 and length16/NONE connectors; packet adapters for
  desktop and Zephyr integration.
- C++ and Python UDP SDKs with owned results and deterministic shutdown.

See the [porting guide](docs/porting-adapters.md) for execution and
ownership, and [development checks](docs/development.md) for validation.
