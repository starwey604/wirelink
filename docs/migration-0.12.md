# Migrating to the 0.12 development contract

Endpoint setup now takes a `wl_link_defaults_t` value explicitly:
`*_endpoint_config_defaults(&config, environment, wl_link_defaults())` and
`*_endpoint_init(&endpoint, environment, wl_link_defaults())` select CRC16.
USB integrations can pass `wl_usb_bulk_link_defaults()` from
`<wirelink/usb_bulk_defaults.h>` to select NONE instead. Both peers must use the
same integrity; there is no negotiation or fallback. Apply application overrides
such as `config.link.integrity = WL_INTEGRITY_CRC32C` after defaults and before
`*_endpoint_init_config()` or `*_endpoint_create()`. Complete configs remain
final. Envelope selection still follows the profile (`any` defaults to native
packets), and storage still reserves the maximum CRC32C trailer. The default
core build remains `multi`; fixed `cobs-crc16` rejects USB NONE with
`WL_ERR_NOT_SUPPORTED`. Raw core configurations can use
`wl_config_apply_link_defaults(&config, defaults)` without resetting other
fields. These signatures replace the old two-argument entry points; regenerate
current code with the matching WLC. Protocol version and schema identities do
not change.

Build Wirelink 0.12.0 and the matching local WLC 0.12.0 source, then regenerate
all codecs, runtimes and C++/Python SDKs together. `wlc codegen-contract` must
print `0.12`. Binding source API revision 2 is a separate identifier. No 0.12
published compiler/source archive pair is pinned; configure
`WIRELINK_WLC_EXECUTABLE` when building installed consumers.

```cmake
find_package(Wirelink 0.12 CONFIG REQUIRED)
```

Pre-1.0 generated contracts must match exactly. Rebuild native SDKs and wheels;
old generated sources are not an ABI compatibility layer. Keep historical
release manifests and wire conformance vectors unchanged.

## Client results and ownership

| Removed path | Replacement |
| --- | --- |
| Client `response_storage`, byte capacity and encoded response pool | Generated `*_rpc_result_value_t` objects, one union per client slot |
| Raw client `response_data` / `response_length` and response ingestion | `wl_rpc_client_response_prepare()` / `wl_rpc_client_response_commit()` |
| Generated `client_decode()` / `decode_owned_response()` | Typed response access and completion copy |
| Completion-time business decoding | One successful RX decode into owned fields |
| Private schema cloning, regeneration and identifier rewriting | Shared codec plans with explicit symbol policies |
| Ordinary-route private codec copies and private canonical hashes | Only managed messages and nested dependencies; public Schema-v1 canonical hashing |

Core validates correlation, deadlines, owner and generation; generated C owns
business types. Custom runtime storage supplies a properly aligned typed result
array separately from its byte arena. Use the generated requirements/default
descriptor, rather than replacing it with a cast byte array.

Ordinary async completion copies the owned result to a separate completion
union, releases admission capacity, then notifies. A callback can immediately
submit another call. Sync copies that completion. Advanced `*_inspect()` borrows
a `const *_value_t *` until release/close; copy `*response` to retain it.
Advanced codec views and deferred-handler request fields remain borrowed;
copy bytes needed after the callback. Server encoded response storage, request
snapshots and detached link TX reclamation remain necessary.

## Guarantees and codecs are independent

Default managed RPC remains tagged/replay. Explicit `guarantee = correlate;`
allows each valid arrival to execute independently and provides no completed
history or RPC duplicate suppression. Both policies reserve pending/response
capacity before invoking the handler and preserve deferred-token generations,
rejection, session checks and delivery backpressure. Cancellation and timeout
end local waiting and cannot undo peer effects.

```text
rpc Setpoint {
  request = SetpointRequest;
  response = SetpointResponse;
  guarantee = replay;
  codec = positional_v1;
}
latest Sample { delivery = unreliable; codec = schema_v1; }
```

RPC codecs are `managed_tagged` (default) or `positional_v1`. Ordinary routes
independently select `schema_v1` (default) or `positional_v1`. Pass the matching
profile to both codec and runtime generation, including CMake's codec `PROFILE`
argument when ordinary routes use positional encoding.

Managed wire v4 retains BE64 originating sessions and full operation32;
metadata uses 9–13 bytes, or 13–17 for rejection. The replaced fixed 20-byte
metadata has no fallback. The default tagged business format remains unchanged.
Positional changes the body/evolution contract: verify schema/profile identities
at deployment, regenerate both peers, and follow [positional-v1](positional-v1.md).
There is no format negotiation or sniffing; a mismatched body can accidentally
decode successfully. Unsupported positional shapes fail before artifact emission.

Latest storage can replace several publications within one owner pass. Products
that must consume each publication should read/release in the result observer or
choose a suitable FIFO; one end-of-pass read does not guarantee every sample.

See [RPC lifecycle](rpc-runtime.md), [storage](endpoint-storage.md),
[compatibility](compatibility.md), and the [round-2 results](codec-rpc-refactor-round-2-result-cn.md).
