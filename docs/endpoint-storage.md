# Optional endpoint creation and destruction

Static `calculator_endpoint_t client = {0}` remains the default. Use creation
when a caller or language binding should not embed the large endpoint object.
[中文](endpoint-storage-cn.md). Codegen ABI 26 uses [automatic sessions](session.md)
and managed RPC metadata v2; creation shares ordinary endpoint initialization.

## The allocator supplies storage, not message lifetimes

```c
calculator_endpoint_t *client = NULL;
calculator_endpoint_config_t config;
calculator_endpoint_config_defaults(&config, wl_platform_environment());
wl_err_t error = calculator_endpoint_create(&client, &config, &allocator);
```

`wl_allocator_t` contains allocate, deallocate and context. Creation makes one
allocation for the endpoint and its bounded protocol storage. Static and created
endpoints share init/step/sync/async; there is no per-field or per-message allocation,
global allocator or implicit heap fallback. This contract covers generated
endpoints/protocol code, not every OS or third-party adapter allocation.

Allocate receives size and alignment and returns writable aligned storage or NULL;
zeroing is not required. Deallocate receives the same pointer, size and alignment.
The descriptor is copied; context lives through destroy. Callbacks must not throw
or reenter the object. Synchronize shared allocators externally or supply a
thread-safe implementation. Ordinary storage does not promise DMA reachability
or cache coherence; adapter-specific memory remains the adapter's responsibility.

## Fixed pool without a heap

Enable CMake `WIRELINK_BUILD_STORAGE=ON` and link `Wirelink::storage`, or Zephyr
`CONFIG_WIRELINK_STORAGE=y`. Include `wirelink/storage/fixed_pool.h`:

```c
static union {
  calculator_endpoint_t alignment;
  uint8_t bytes[2 * sizeof(calculator_endpoint_t)];
} memory;
static wl_fixed_pool_t pool;
wl_fixed_pool_init(&pool, memory.bytes, sizeof(memory.bytes),
    sizeof(calculator_endpoint_t), CALCULATOR_ENDPOINT_ALIGNMENT, 2);
wl_allocator_t allocator = wl_fixed_pool_allocator(&pool);
```

Check setup return values. The pool supports up to 64 blocks; stride rounds block
size up to alignment. Init checks alignment, rounding/multiplication overflow and
total storage, leaving the pool unchanged on failure. Metadata stays separate
from live endpoints. Exhaustion makes create return NO_MEM; freed blocks can be
reused. The pool is single-owner, with no internal locks. Destroy every allocation
before reinitializing its metadata or reclaiming the backing memory.

## Lifetime and rollback

Create requires `*out == NULL`; failure leaves it unchanged and rolls back its
allocation if initialization fails. Temporary config/allocator descriptors are
fine, but handler, clock and waiter contexts retain their individual lifetimes.
Size/alignment depend on the target and capacity macros: use one configuration
consistently across all translation units.

```c
/* After stopping/joining the background owner, all callers and producers: */
wl_err_t error = calculator_endpoint_destroy(&client);
/* Success: client == NULL, no remaining notification or adapter borrow. */
```

Destroy runs generated close/quiesce before freeing. A NULL owner is a no-op;
static objects use close, not destroy. Destruction from its own callback returns
REENTRANT and leaves the pointer valid. With a background executor, stop/join
first; destroy is not a cross-thread shutdown mechanism. Pointer aliases, old
cancel handles and tokens become invalid after destruction, even if a later
allocation reuses the address. Close/reinit on the same allocation preserves its
allocator ownership, but a new session still needs an appropriate session ID.
Copied owned responses survive destruction without any destructor or allocator.

## Narrow C++ and Python bindings

[`tests/endpoint_storage`](../tests/endpoint_storage/) contains exported C,
C++ RAII and a Python context manager. Python does not supply hot-path callbacks
or depend on C struct layout. The binding owns orderly shutdown and paired
destruction, not a full SDK or GC-timed shutdown scheme. Each consumer runs 2000
sync calls with creation-only allocator use and retained results, and injects
second-endpoint failure to check whole-binding rollback.
