# Host owner and C++ Session

[中文](host-runtime-cn.md) · [SDK example](../examples/06_bindings/GUIDE.md)

Use `wirelink::host::Executor` to run a C kernel on one owner thread, or
`wirelink::Session` for an owning UDP connection with typed results. Enable
`WIRELINK_BUILD_HOST_RUNTIME=ON` for Executor; `WIRELINK_BUILD_CPP_BINDINGS=ON`
adds Session and its Asio dependency. See [build commands](development.md#build-the-host-owner-session-and-native-sdks).

## Give the owner bounded work

[`Executor`](../runtime/host/include/wirelink/host/executor.hpp) binds a C kernel,
I/O callbacks and a clock descriptor. The caller owns the kernel until `start`
succeeds, and keeps its storage alive until stop/join. `post` admits a command;
its work or shutdown rejection runs on the owner. It is separate from kernel RPC
admission. The queue has **32 entries**, with at most **8 commands** before kernel
work. Commands must not throw, block or call `stop`.

I/O notify is nonblocking, latched and thread-safe; it must not reenter the owner
or application callbacks. Service/readiness/wait combine I/O, core and application
deadlines. A custom clock shared by submitters and the owner must be thread-safe;
advancing a manual clock also requires `notify`.

## Let Session own connection lifetime

[`Session`](../bindings/cpp/include/wirelink/cpp/session.hpp) uses a C endpoint
factory, the same Executor and kernel. C owns atomic packet/FIFO storage; C++ uses
opaque pointers. Initialization obtains a fresh nonzero instance from an explicit
platform random or durable source. Failure does not fall back to a clock or fixed
production ID; comparing one previous identity is only a local guard.

Submit waits for owner admission, not a remote response. Success snapshots input;
failure creates no call or future callback. Sync/async calls share four kernel
call slots. C++ timeouts use milliseconds in `1..INT32_MAX` and include command
queue time. An `Operation<T>` owns its copied result beyond client destruction;
dropping it does not cancel. Local cancel/timeout cannot undo remote execution.
See [RPC results](kernel-api.md#terminal-results).

## Close outside the owner callback

Owner callbacks may request stop but must not join, destroy or replace their own
Executor/Session. An owner-side Session close requests stop; an external caller
performs the join. External Session close excludes/waits for submitters, closes
logical work, stops actual I/O, processes final completion and joins the owner
before releasing C storage. Kernel quiescence alone does not establish external
notifier exit. Restart uses new storage initialization and a fresh instance.

The [development guide](development.md) selects host, Session, installed-consumer
and Python wheel checks; [adapters](adapters.md) define real I/O stop/drain.
