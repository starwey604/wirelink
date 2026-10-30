# Native USB raw echo and application object sink

This diagnostic sample uses the default C packet kernel and native/NONE USB
adapter. It echoes opaque ordinary DATA payloads up to 2048 bytes, and handles
BulkBegin/Chunk/End/Abort (IDs 32–35) with ordinary BulkStatus (36). These are
application commands, not a framework Bulk service. There is no RPC service,
ARQ, ACK state, automatic reliable transfer or automatic retry.

The complete public `tests/fixtures/wlc/current/control.wl` schema and its message
IDs/encodings are unchanged. WLC 0.13 generates its pure codec. In particular,
`bytes<4096>` remains a legal public schema domain; this deployment separately
limits each actual object chunk to min(requested size, 2016), and the native
packet payload to 2048. No generated bound is silently reduced to fit transport.
Unrelated messages are opaque echoes, without requiring a bounded typed route
for the schema's unbounded ArmCommand fields.

`src/application.c` owns a sequential pattern/CRC32C sink with a 64 MiB object
limit, one-second idle timeout, one pending output, one current transfer and the
last completed transfer identity. It validates `(uint8_t)offset ^ 0xa5` data,
checks duplicate chunks against that pattern without a second write, rejects
out-of-order chunks, commits once and accepts a matching repeated END. The
caller must retry lost commands/status and provide a suitable application ID;
this bounded demonstration is not durable or general exactly-once storage.
A real flash/file sink must implement its own persistence and duplicate checks.

The owner copies each borrowed input before returning, then admits output into
the kernel's ordinary snapshot. If output is still pending, a one-action step
only submits the occupied snapshot or consumes its physical completion; it does
not consume another command. A BUSY submission waits for actual activity. The
two packet RX units then apply USB backpressure. This gate belongs to this raw
tool with no RPCs, not to the kernel's RPC scheduler. No second execution engine,
reply token, pending-call table or synthetic completion is introduced.

The owner obtains a fresh identity outside the kernel, merges application and
I/O wake conditions and rebases finite waits. RX boundary faults trigger an
explicit full USB re-enumeration, preserving the kernel instance and physical
leases. Close aborts the application job and joins real USB cancellation before
port/adapter storage can be reused. The deterministic object completion record
retains the `wirelink_object_rx_v1` CSV format; only the most recent completed
object report is retained, and printing waits for physical TX drain.

CPU telemetry is optional. `wirelink_usb_kernel_cpu_v2` counts the application
pass (including one bounded kernel step), adapter callbacks, sink and service.
Driver hooks are optional and zero means unobserved. Atomic cycle sums are
explicitly modulo 32 bits; application pass totals use 64 bits. The output does
not claim the former polling-stage definitions, an end-to-end active CPU total,
or the dedicated K4 RPC/Sample benchmark's workload. CPU-on and CPU-off builds
must be measured independently. No Zephyr/Astrial vendor files are changed.

Twister builds both ESP32-S3 observer configurations with matching local WLC.
`tests/usb_sample` runs the actual application with the C kernel and a controlled
physical driver, including full-size echo, retained-source completion/drain,
ordered backpressure, no-spin BUSY, chunk duplicates, CRC, once-only commit,
wrapped idle timeout and close. Builds and RAM-model tests do not qualify a
physical USB connection. The dedicated static-kernel hardware benchmark remains
the source for RPC/Sample performance and final-image handoff evidence.
