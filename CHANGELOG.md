# Release notes

## 0.13 development

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

See the [kernel API](docs/static-endpoint-kernel-api.md) for execution and
ownership, and [development checks](docs/development-current.md) for validation.
