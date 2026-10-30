# Packet transport tools

These tools build the current core and packet adapters. They require a local
matching WLC 0.13 executable; no published compiler is assumed:

```sh
cmake -S . -B build/packet-tools -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_PACKET_TOOLS=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_BUILD_ASTRIAL_ADAPTER=ON -DWIRELINK_BUILD_ASTRIAL_USB_ADAPTER=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc
cmake --build build/packet-tools --parallel
build/packet-tools/benchmarks/host/wirelink_transport_benchmark --help
ctest --test-dir build/packet-tools --output-on-failure
```

`wirelink-bulk` retains its historical CLI spelling for an ordinary raw echo
workload. It uses native/NONE USB packets, one outstanding application message,
up to 2048 payload bytes, and requires the matching current USB sample.
`raw-bulk` remains an independent Astrial byte echo probe, without a Wirelink
core. Its driver worker closes before borrowed buffers or callback state die,
including partial-start failure and timeout paths.

`wirelink-object` sends the public Control BEGIN/CHUNK/END messages (IDs 32–36),
with the unchanged business schema. The application chooses at most 2016 data
bytes per chunk, at most 64 MiB per object, and validates CRC32C at commit.
Each command allows three application attempts, with a 250 ms Status deadline
and the configured overall object deadline. Only the matching transfer ID,
phase and next offset completes an action; actual local physical TX must also
finish. Lost Status may repeat a command. The matching sample owns its duplicate
chunk/END behavior and commits a completed transfer once. Other applications
must define their own idempotency. Failure or local close does not roll back a
remote write. Reuse of transfer IDs across independent runs is the caller's
responsibility; `--transfer-id-base` makes that choice explicit.

`cdc` is the historical CLI spelling for an explicit COBS/CRC16 serial echo
connection. `wirelink_astrial_typed_serial SERIAL_PORT` sends the same public
six-joint ArmCommand, with position/velocity/torque/kp/kd/mode, sequence 1 and
enabled true. It reports local physical TX completion, without a remote ACK.
These Linux example assemblies require a nonblocking `TIOCSERGETLSR` /
`TIOCSER_TEMT` physical drain query. Unsupported drivers (including many PTYs
and USB serial implementations) fail before adapter I/O starts. Other systems
need their own verified physical drain query. The current Zephyr CDC sample is
a capability probe, not a compatible echo peer.

`--wake all` is required. RX-only notification suppression is no longer an
option. `poll` continuously services the owner; `wait` and `hybrid` currently
use the same latched finite-deadline wait. The removed `--spin-us` option explicitly errors. Reports carry
`wirelink-packet-transport-v1` / `wirelink-packet-object-v1`; they must not be
pooled with the removed ARQ/Bulk sender workload.

C owns the kernel, all atomic port/FIFO objects and reply snapshots in
`examples/common/packet_tool_owner.c`; C++ drives one owner and the real adapter.
Only one tool assembly is active per process. Closing first closes the core,
then stops/joins actual I/O, and finally processes cancellation to quiescence.
The controlled `packet_tool_owner` and `packet_object_client` tests exercise
real RAM copies, separate consumed/drained events, BUSY, source poisoning,
stale phase filtering, bounded retries and one commit. They do not qualify
physical USB or serial hardware.
