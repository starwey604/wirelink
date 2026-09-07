# Automatic session identity and platform environment

Ordinary applications do not select or retain session IDs. Codegen ABI 26 acquires
a fresh identity at endpoint initialization. [中文](session-cn.md). No new release
is published; both managed-RPC peers must use matching builds.

## Default integration

```c
#include "calculator_endpoint.h"
#include "wirelink/platform.h"
static calculator_endpoint_t client;
int error = calculator_endpoint_init(&client, wl_platform_environment());
```

Link `Wirelink::platform` (`WIRELINK_BUILD_PLATFORM=ON`) on desktops, or enable
`CONFIG_WIRELINK_PLATFORM=y` on Zephyr. The environment contains a monotonic
millisecond clock and an identity source. Windows/macOS use system RNGs; Linux
uses nonblocking getrandom. Zephyr requires a real CSPRNG: unavailable or test-only
sources return NOT_SUPPORTED, without a weak fallback. Only initialization reads
randomness; packet processing adds no random reads, threads or heap allocations.
Initialize on the owner/task, not an ISR. A Zephyr RNG driver may wait for hardware
during initialization; this is not a hard-real-time initialization guarantee.

Use `endpoint_config_defaults(&config, wl_platform_environment())` to register
handlers, then `endpoint_init_config()`. Reusing a configuration still obtains a
new identity per init/create. Override `config.environment.clock` to inject time;
transport and RPC deadlines continue to share that source.

## Bare-metal/custom source

`wl_environment_t.session` is a C `wl_session_source_t` with a
`wl_err_t (*next)(void *context, uint64_t *identity)` callback. Supply a fresh,
nonzero identity using board entropy or a durable, power-fail-safe identity
allocator. This belongs to platform integration, not each business service.
Descriptors are copied; their contexts must remain alive when used. Shared sources
must synchronize themselves. Do not throw or reenter an endpoint from callbacks.

Provider errors, zero IDs and repeating the same object's previous identity fail
initialization. Checking one previous ID is not a uniqueness proof. Random
collisions are possible; deterministic test sources are not production boot
identity providers. Never silently substitute a fixed value, uptime or address.

## Lifetime and RPC scope

The identity names an instance, not an address, key or ordered version. It stays
unchanged through retransmission and ordinary link disconnection. After stopping
producers and orderly close, reinitialization obtains a fresh identity. Do not
copy/move a live endpoint.

Managed RPC v2 carries the originating client identity in requests and all replies,
including rejection, cached replay and deferred completion. Responses match both
that identity and the call number for all delivery combinations. Reliable request
metadata must also match the link sender identity. Stale responses are observable
as SESSION_MISMATCH but cannot complete or fail another call. This is not
authentication, arbitrary stale-request rejection or durable exactly-once execution.

Operation-ID exhaustion explicitly rejects new calls; existing calls can drain.
Close/reinitialize at an owner safe point to obtain a fresh instance without
calculating an ID. The library does not silently restart resources from a callback.
Advanced raw link/runtime integrations still explicitly own their identity policy.

## Migration

Replace `init(endpoint, session_id, clock)` with `init(endpoint, environment)` and
`config.clock` with `config.environment.clock`. Leave the ordinary configuration's
`link.session_id` zero: initialization fills an internal copy. Regenerate all
consumers for ABI 26. Managed metadata v2 is 20 rather than 12 bytes and does not
interoperate with v1: upgrade both peers. Maximum one-frame business capacity
shrinks by eight bytes. Mapped RPC, business codec bytes and Compact-v1 frames are
unchanged; mapped responses do not automatically acquire this new protection.
