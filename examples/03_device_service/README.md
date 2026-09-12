# A device service that grows without a new dispatcher

This is a maintainer-facing acceptance example, not a benchmark. Two standalone
C11 programs communicate over UDP and exercise 12 managed RPCs: device queries,
settings, registers, arithmetic and one genuinely deferred self-test. No hardware
is accessed. [中文阅读指南](README-cn.md).

Start with `get_info()` in [device_service.c](device_service.c), then read
[server.c](server.c), [services.bind.wl](services.bind.wl), [device.wl](device.wl)
and [client.c](client.c). Read [self_test.c](self_test.c) last: only deferred work
needs completion authority. Ordinary handlers return their response/status.

Both endpoints reuse one RPC contract. CMake composes that file with
`client.bind.wl` (LATEST reception) or `server.bind.wl` (send-only telemetry).
The outgoing 100-channel message is larger than every RPC: its bound must size
the default endpoint even though the sender has no receive mailbox.
Both local profiles select `native_packet` and their own `rpc_role`. WLC removes
unused transport/RPC storage; handlers and client calls need no extra switches.
See [layout options and memory results](../../docs/endpoint-layout.md).

Set `config.user_data` once for ordinary handlers. Non-null per-service
`<service>_user_data` overrides it; NULL inherits. Advanced deferred contexts and
per-call completion contexts stay explicit. Business modules register their own
handlers; transport, RPC dispatch and result recycling are not application code.

## Build and run

Use the matching development WLC 0.7.0-dev / codegen ABI 32 and standalone Asio.
ABI 32 has no published bootstrap source pair yet: build paired WLC commit
`18b830af2cdd535bdfc6e2bbd3f147f9a9a4ce29` in its separate workspace with
`cargo build --release --locked`, verify `wlc --version` and `wlc codegen-abi`,
and pass its executable explicitly. No nested WLC checkout is required.

```sh
cmake -S . -B build/device-service -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build build/device-service --config Release --parallel 2
```

Run `device_service_server` and `device_service_client` in separate terminals;
executables are under `build/device-service/examples/03_device_service/`.
Windows uses `.exe`; multi-config generators add `Release/`. Default loopback
ports are 49300/49301; both accept `local_port peer_port`. Restart the server
before repeating the full manual client, which changes simulated device state.

## Add another RPC

The [extension test](../../tests/tutorials/device_service_extension.py) adds
GetBuildLabel to a temporary copy and rebuilds/runs the real programs. Exactly
four application files change: message definitions, the shared RPC binding,
one handler plus its registration, and a client use case. Server main, transport,
other business modules and CMake remain byte-identical. There is no operation
enum, dispatcher switch, manual buffer sizing or release branch to update.

```sh
ctest --test-dir build/device-service -C Release -R wirelink_device_service --output-on-failure
```

All original cases and the new service must pass. This tests extensibility, not
unlimited capacity: messages still need unique IDs and bounded single-frame
payloads. Concurrent calls retain the default four-slot limit. Timeouts include
queuing. Do not call sync recursively from handlers; real workers hand deferred
results back to the endpoint owner. Telemetry may drop under backpressure; this
example does not implement product-specific fair scheduling.
