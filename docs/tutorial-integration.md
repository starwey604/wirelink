# Integrating an endpoint

Build a typed endpoint with a matching local WLC and an explicit profile v2.
One `wirelink_wlc_generate` target supplies its codecs, routes and storage bounds.

1. Allocate the generated endpoint, C-owned packet port and two RX units.
   Initialize the port with a fresh nonzero instance identity.
2. Bind and start the adapter after its storage and wake context are ready.
3. From one owner, service the adapter and call `wl_kernel_step` with monotonic
   time. Merge kernel, adapter and application deadlines before a latched wait.
4. On shutdown, close the kernel and stop/join I/O producers. Process remaining
   completions until the kernel is quiescent, then release storage.

Follow [installation](installation.md) for CMake setup and the
[telemetry tutorial](getting-started.md) for a running endpoint.
The [kernel API](static-endpoint-kernel-api.md) defines admission, callbacks and
shutdown; [adapter lifecycle](adapters.md) defines physical buffer ownership.
