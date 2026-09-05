# Lesson three: use your own project and hardware

You can now [receive temperature](getting-started.md) and
[request a calculation](tutorial-rpc.md). This lesson moves that code into an
independent project, then explains replacing the in-memory connection.
Build on the desktop first to separate build issues from hardware issues.
[中文](tutorial-integration-cn.md).

## 1. Why separate codec and runtime?

Ordinary applications use the default endpoints from the first two lessons.
This section explains their internals and build dependencies, not extra objects
you need to assemble manually.

The temperature program performs two kinds of work:

- Convert `telemetry_t` to bytes and back: the **codec**, encoder/decoder.
- Retain received temperature or track pending RPC calls: the generated **runtime**.

A send-only sensor needs encoding and sending, not the display's LATEST storage.
A display and recorder may share messages but handle reception differently.
Several runtimes can therefore share one codec without duplicate encoding symbols.

A CMake **target** names a buildable group of files or program, not a hardware
board. `temperature_codec` is a library made from generated C;
`temperature_protocol` contains the selected receive helpers and depends on it.
Choose target names freely. Generated `temperature_` prefixes default to the
schema filename, not the CMake target name.

## 2. Build an independent display project

Create a directory containing four files:

| File | Contents |
| --- | --- |
| `main.c` | Copy the complete [`latest_telemetry.c`](../examples/latest_telemetry.c) |
| `temperature.wl` | [Message schema](../examples/getting_started/temperature.wl) |
| `temperature.bind.wl` | [LATEST configuration](../examples/getting_started/temperature.bind.wl) |
| `CMakeLists.txt` | The complete configuration below |

```cmake
cmake_minimum_required(VERSION 3.21)
project(temperature_display LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Wirelink CONFIG REQUIRED)
wirelink_wlc_generate_codec(
  TARGET temperature_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/temperature.wl")
wirelink_wlc_generate_runtime(
  TARGET temperature_protocol
  CODEC_TARGET temperature_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/temperature.bind.wl")
add_executable(temperature_display main.c)
target_link_libraries(temperature_display PRIVATE
  temperature_protocol Wirelink::loopback)
```

`wirelink_wlc_generate_codec()` reads the schema, invokes WLC, and compiles
encoding, decoding, and typed sending code. `wirelink_wlc_generate_runtime()`
reads the binding profile and generates telemetry reception and LATEST access.
`CODEC_TARGET` selects the shared message code; `PROFILE` selects handling
configuration. Linking the runtime brings its codec and Wirelink core dependencies.
`Wirelink::loopback` supplies the example's in-memory transport.

At the Wirelink root, build all libraries (lesson one built only the telemetry
target), then install into a local directory:

```sh
cmake --build build/quickstart --parallel
cmake --install build/quickstart --prefix "$PWD/build/tutorial-install"
```

Build your project, substituting your real absolute paths:

```sh
cmake -S /path/to/temperature-display -B /path/to/temperature-display/build \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/wirelink/build/tutorial-install \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
cmake --build /path/to/temperature-display/build
/path/to/temperature-display/build/temperature_display
```

Expect `latest telemetry: sample=2 temperature=23.50 C` again.
WLC runs at build time; generated C becomes part of the executable or firmware.
This development branch needs codegen ABI 20; install WLC independently on PATH
as described in [environment setup](installation.md).

Projects using typed sending and custom reception can link only the codec.
Add runtimes as needed. See the [WLC guide](https://github.com/starwey604/wlc/blob/6c992decc4b200d258bd8c7409a8896ab37a17e8/README.md) for role separation
and naming options.

## 3. Two different configurations

The `.bind.wl` **binding profile** selects LATEST, FIFO, and RPC message handling.
A **FIFO** is a bounded first-in, first-out queue for processing received items
in order. A full queue still requires failure handling; it is not unlimited
or an unconditional no-loss guarantee.

`wl_config_t` configures how the connection transfers packets. References may
call these settings a link profile; they are not the binding-profile file.

| Field or term | Meaning | Selection |
| --- | --- | --- |
| `envelope` | How reception identifies packet boundaries | Match whether the transport preserves complete packets |
| `integrity` | Detection of accidental byte corruption | Agree on a mode, for example CRC32C |
| `max_payload_len`, payload bound | Maximum encoded message size without headers/checksum | Cover messages sent and received |
| `max_transmission_unit` | Maximum complete transport packet size | Include headers, checksum, and envelope overhead |
| `session_id` | Distinguish reliable traffic from this boot and older boots | Use the nonzero identity policy below |
| `ack_timeout_ms`, `max_retries` | How long to wait before retrying and how often | Account for transport delay, scheduling, and recovery needs |

“Out of band” means your programs agree on settings beforehand; v1 does not
exchange and negotiate them. Using matching envelopes, checksums, and capacities
at both ends is easiest initially. Capacities are local limits, not necessarily
identical protocol constants: every outgoing packet must fit the transport and
the other end's receiving capacity.

Default endpoints derive the payload bound from profile-selected messages and
reserve for every supported envelope, a one-packet stream RX buffer, and runtime
storage. Start with `endpoint_config_defaults()`, change `config.link.envelope`
or other settings, and call `endpoint_init_config()`. RPC roles, timeouts, and
handlers remain explicit in `config.runtime`; retry policy is not invented.

`*_HAS_DEFAULT_ENDPOINT` is 1 when the full default type is available. Unbounded
selected messages or bounds above the 2048-byte frame limit set it to 0.
Unselected oversized messages do not inflate the endpoint. To handle other
messages, adjust the profile or use advanced codec/custom reception.

For DMA-specific memory, deeper RX queues, or more concurrency, use advanced
assembly: query `wl_config_requirements()` and `runtime_requirements()`, supply
external storage, and compose link/application hooks with `wl_endpoint_init()`.
Default endpoints do not resize automatically at runtime.

CRC detects corruption; it neither authenticates senders nor encrypts content.
Implement those requirements in your product's transport/security layer.

<a id="session-identity"></a>

### Choosing session identities across real reboots

Suppose a device restarts while old packets or acknowledgements remain in the
connection. Reusing sequence numbers alone could let an old ACK confirm new work.

`session_id` identifies one boot or communication instance. Reliable data
and acknowledgements carry the relevant session identity so the protocol can
distinguish old-session traffic. Nonzero means simply that 0 is reserved as invalid.

It is not an address selecting which device receives a packet. Wirelink connects
two ends and provides no node-address routing. The isolated example uses fixed
0x1001 and 0x2002 values for repeatability, not a production reboot policy.

One approach generates a fresh nonzero random value each boot, often called a
**boot nonce**: a random identifier for this startup. Another increments a
persistent boot counter before using it. Avoid reusing an identity while old
traffic might survive; random generation must account for collision probability.
This identifier is not authentication or an encryption key.


## 4. Replace loopback with real transport

An **adapter** connects Wirelink to a driver: submit outgoing bytes to hardware
and publish incoming bytes to Wirelink.

| Interface behavior | Envelope | Reason |
| --- | --- | --- |
| UART or USB CDC serial byte stream | `WL_ENVELOPE_COBS_STREAM` | COBS encoding and delimiters recover packet boundaries from continuous bytes |
| Whole-packet delivery, for example UDP datagrams | `WL_ENVELOPE_NATIVE_PACKET` | The interface already preserves boundaries |
| Bus using Wirelink's supported 16-bit length-prefix format | `WL_ENVELOPE_BUS_LENGTH16` | Length fields delimit packets |

Do not select by the names “USB” or “CAN” alone. CDC is a stream; other USB or
CAN adapters need the appropriate segmentation/reassembly to deliver whole
Wirelink packets. Choose an [existing adapter](adapters.md) and check its input contract.

A custom adapter registers a send entry with `wl_set_sink()`.
Return `WL_SINK_SENT` for synchronous consumption, or `WL_SINK_STARTED` while
the driver borrows the bytes, followed by exactly one owner-side `wl_tx_complete()`.
`WL_SINK_BUSY` means temporary backpressure; `WL_SINK_FAILED` reports I/O failure.

Publish packets using `wl_feed_unit()`, or streams using `wl_feed_bytes()`.
DMA and reduced-copy ingress are later optimizations after this path works.

## 5. Keep communication moving

The default path calls only `*_endpoint_step()`: attached adapter service, event
cleanup, and runtime progress are included. `*_endpoint_handle()` supplies the
common endpoint; use `wl_endpoint_get_hint()` before sleeping. LATEST/FIFO
`endpoint_read_*()` returns an owned copy. Advanced zero-copy reading remains
available through `endpoint_runtime()` and existing acquire/release calls.

To integrate an existing hardware adapter, obtain its core pointer with
`wl_endpoint_link(endpoint_handle(...))`, then install its service/quiesce/deadline
hooks using `wl_endpoint_attach()`. Loopback's `wl_loopback_connect()` already
performs both steps; other platforms still need this integration glue.
The rules below constrain that integration, not ordinary business code.

Choose one communication thread or bare-metal loop per connection, the **owner**.
Sending, pump work, RPC operations, and TX completion notifications execute there.
Interrupts/driver callbacks may publish RX bytes, record TX completion, and wake
the owner; they must not execute RPC handlers.

Each application round:

1. Read your monotonic millisecond clock once.
2. Service adapter completions and run `wl_pump_step()`. Connect adapter service
   through pump hooks, or call it explicitly as the examples do.
3. Read latest values or RPC states and perform bounded application work.
4. Before sleeping, call `wl_pump_get_hint()` for immediate work and timeout timing.
   Also wake on external reception, TX completion, and writability events.

Generated `runtime_pump_hooks()` connects dispatch, receive-event release,
matching RPC TX-result reclamation, pending responses, and RPC timeout progress.
It creates no thread: your program must keep running the pump. Avoid long handler
work; defer slow calculations to an application task and return results to the owner.

Do not manually release receive events or take TX terminals already handled by
the generated pump. See [API boundaries](api-boundary.md) for manual ownership.

At shutdown, stop producers and end outstanding transport access to borrowed
storage, called **quiescing**, before destroying or reinitializing endpoint
storage. Initialized connections, runtimes, and their storage must not be moved or copied.

## 6. Diagnose initialization and peer changes

Generated `runtime_init_checked()` adds diagnostic output to ordinary init.
Its `issue`, `field`, `required`, and `provided` identify invalid configuration
and insufficient capacities. Once proven, ordinary init provides the smaller path.

Check function returns, pump `poll_errors` / `service_errors`, and runtime
result callbacks first. Link optional `Wirelink::diagnostics` for counter
formatting into your logger; see [diagnostics](diagnostics.md).

Default `endpoint_step()` returns per-pass errors. Get runtime details through
`endpoint_result()` and core progress through `wl_endpoint_last_step(endpoint_handle(...))`.
Later successful events cannot overwrite the first runtime failure in a pass.
The endpoint reclaims reliable TX terminals; do not take them again. Choose
advanced manual scheduling if you need to manage transport handles yourself.

On RPC peer changes, `rpc->peer_changed` signals a transition.
`runtime_peer_observation_take()` retrieves previous/current session IDs.
The runtime cleans RPC state; the product handles business effects such as
revoking old control authority. If reliable non-RPC traffic establishes the
same product session, explicitly call `runtime_peer_observe()` before applying
it. Prefix these function names with the generated module, such as `quickstart_`.

## References by need

You have now received state, requested work, and integrated a build and driver.
Use references as needed:

- [API boundaries](api-boundary.md) for public interfaces and ownership.
- [Schema](schema-v1.md) and [WLC](https://github.com/starwey604/wlc/blob/6c992decc4b200d258bd8c7409a8896ab37a17e8/README.md) for more message definitions.
- [LATEST](latest-mailbox.md) and [FIFO](fifo.md) for retained-storage limits.
- [RPC runtime](rpc-runtime.md) for failures and retries.
- Bulk in [application-layer](application-layer.md) for large objects.
- [Protocol](protocol.md) and [compatibility](compatibility.md) for wire changes and upgrades.
