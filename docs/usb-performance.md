# Measuring USB Bulk

The [host transport tools](../benchmarks/host/README.md) exercise ordinary
packets, RPC and application object transfer over USB Bulk. Build host and
device from matching schemas and profiles, then retain complete captures with
their source revisions, generated output, firmware and build configuration.

Record RPC round-trip distributions, useful payload throughput and CPU cost
separately. Record clock, I-cache configuration, optimization level, LTO,
endpoint sizes and transfer sizes with each run. Keep instrumented CPU runs
separate from uninstrumented latency and throughput runs.

The USB adapter uses native/NONE packets. Preserve physical TX completion,
RX read headroom, MPS/ZLP handling and boundary-fault recovery throughout the
measurement. See [adapter lifecycle](adapters.md) for these requirements and
[benchmark procedures](../benchmarks/README.md) for collection and comparison.
