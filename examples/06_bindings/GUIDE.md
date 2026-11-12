# Run the Calculator C++ / Python SDK

Call the C service over UDP and print `42`. This hand-maintained guide covers
building, testing and extending the example; [README.md](README.md) is generated
from WLC's SDK template. The Python distribution is `wirelink-calculator-sdk`,
imported as `calculator_sdk`.

Use matching **WLC 0.13.0**, generated contract **0.13**, binding source API **3**
and **Wirelink 0.13.0**. This example's package version is **0.13.0**.
Run shell commands from the Wirelink root. SDK authors need C11/C++20 compilers,
CMake 3.21+ and standalone Asio headers; Python builds need Python 3.10+ with
its development headers.

## Install native prerequisites

```sh
cmake -S . -B /tmp/wirelink-sdk-core \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/tmp/wirelink-sdk-install \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DWIRELINK_BUILD_CPP_BINDINGS=ON \
  -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_BUILD_EXAMPLES=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build /tmp/wirelink-sdk-core --config Release --parallel
cmake --install /tmp/wirelink-sdk-core --config Release
```

`Wirelink::cpp` supplies result/error types; `Wirelink::cpp_host` supplies the
owning Session. Core-only builds do not discover Python. Static native dependencies
need PIC for a Linux extension. On Windows, use matching compiler and dynamic CRT
settings for the SDK and its dependencies.

## Build the native SDK and C peer

```sh
cmake -S examples/06_bindings -B /tmp/wirelink-calculator-sdk \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/wirelink-sdk-install \
  -DCMAKE_INSTALL_PREFIX=/tmp/wirelink-sdk-install \
  -DBUILD_TESTING=ON
cmake --build /tmp/wirelink-calculator-sdk --config Release --parallel
ctest --test-dir /tmp/wirelink-calculator-sdk -C Release --output-on-failure
cmake --install /tmp/wirelink-calculator-sdk --config Release
```

Start the test service in another terminal:

```sh
/tmp/wirelink-calculator-sdk/tests/calculator_c_server
```

It prints its ephemeral UDP port on the first line. Keep the process running;
use that printed number below. The service learns its first client's peer, so
restart it before trying another client. On multi-config generators, executables
are under `tests/Release/`.

## Build and use the Python package

```sh
python3 -m venv /tmp/wirelink-sdk-venv
/tmp/wirelink-sdk-venv/bin/python -m pip install build nanobind scikit-build-core pytest
/tmp/wirelink-sdk-venv/bin/python -m build examples/06_bindings \
  --outdir /tmp/wirelink-sdk-dist \
  -Cbuild-dir=/tmp/wirelink-sdk-python-build \
  -Ccmake.define.CMAKE_PREFIX_PATH=/tmp/wirelink-sdk-install
/tmp/wirelink-sdk-venv/bin/python -m pip install /tmp/wirelink-sdk-dist/*.whl
```

On Windows use the venv's `Scripts/python.exe` and absolute paths appropriate to
your machine. A source package includes generated C and needs no WLC/Rust checkout;
its build still needs the matching installed Wirelink/Asio development package.
Build isolation supplies Python build dependencies, not native Wirelink libraries.
A wheel privately links native static code and needs a matching CPython/platform
at runtime, without a compiler or runtime Python dependencies.

Set `PEER_PORT` to the service's printed number and run this script with the venv:

```python
import os
from calculator_sdk import Client, Udp, RejectedError

with Client.connect(Udp(peer=("127.0.0.1", int(os.environ["PEER_PORT"])))) as client:
    answer = client.add(left=20, right=22, timeout=1.0)
    print(answer.sum)  # 42
    try:
        client.add(left=2**31 - 1, right=1)
    except RejectedError as error:
        print("service rejection:", error.status)  # 1

print(answer.sum)  # 42, owned data remains valid after close
```

`AddRequest(left, right)` with `client.add_request(request)` is the message-object
entry. Fields are required signed 32-bit integers; booleans are rejected as integer
inputs. `AddResponse` is immutable. The package includes annotations and `py.typed`.

UDP endpoints use numeric IP addresses. Peer port is required; bind port zero
selects an ephemeral local port, exposed as `client.local_port`. `connect()` opens
local resources, without discovery or a remote handshake. `is_open` describes
local lifecycle state, not peer liveness. Create a new Client after closing one.

## Run concurrent operations

Restart the C peer before this separate client and update `PEER_PORT`:

```python
import asyncio
import os
from calculator_sdk import AsyncClient, Udp

async def main():
    async with AsyncClient.connect(Udp(peer=("127.0.0.1", int(os.environ["PEER_PORT"])))) as client:
        results = await asyncio.gather(
            client.add(left=20, right=22),
            client.add(left=-10, right=52),
        )
    print([value.sum for value in results])  # [42, 42]

asyncio.run(main())
```

Create AsyncClient in its event loop. It has one completion thread that wakes the
loop; the native owner never executes Python. Blocking Python waits release the
GIL. Sync/async methods share **four native call slots**. AsyncClient additionally
bounds **eight undelivered operations**, including completed results not yet
handed to the loop. Native capacity may be exhausted earlier. `QueueFullError`
reports capacity pressure; retry after other operations complete, if appropriate.
The host's 32-entry command queue and batches of at most 8 commands do not add calls.

Python timeouts are seconds, rounded up to milliseconds, greater than zero and at
most **2147483.647 seconds**. They include owner-command queue time. Expiry raises
`RpcTimeoutError`. An asyncio task cancel requests native cancellation and raises
`asyncio.CancelledError`; explicit native operation cancel produces `CancelledError`.
Closing completes pending admitted calls with **`ClosedError`**.
Local cancellation and timeout do not undo a remote side effect; the SDK does not
retry whole RPCs automatically.

Use `with`, `async with` or explicit close for deterministic cleanup. Native close
rejects new calls and joins owner/I/O while coordinating admitted callers; close
is idempotent. Calls and close support concurrent use. Never block, join or destroy
the Session from its own owner callback.

Python `WirelinkError` exposes `kind`, `status`, `outcome`, `os_error` and
`os_message`. Specific exceptions include `ClosedError`, `CancelledError`,
`RpcTimeoutError` (also a built-in TimeoutError), `QueueFullError`, `RejectedError`,
`TransportError`, `CodecError` and `InvalidArgumentError`. Type/range validation
raises TypeError/ValueError before admission. Business rejection status and local
completion outcome remain separate.

## Link a C++ consumer

```cmake
find_package(CalculatorSdk 0.13.0 CONFIG REQUIRED)
target_link_libraries(app PRIVATE CalculatorSdk::client)
```

Given an open `calculator::Client client`:

```cpp
auto admitted = client.add_async({20, 22}, std::chrono::seconds(1));
if (!admitted) { /* inspect admitted.error() */ }
else {
  auto operation = std::move(admitted).value();
  auto result = operation.result(); // wait; owns AddResponse or Error
  if (result) { /* use result.value().sum == 42 */ }
}
```

Client is movable, not copyable; move/destruction require exclusive access to its
handle. `Operation<T>` is copyable and supports `done`, `wait`, `wait_for`, `result`
and `cancel`. Dropping it leaves admitted work owned by Session. Input snapshots
exist before successful submission returns, and results outlive the client.
C++ timeout is milliseconds in `1..INT32_MAX`. Error holds `kind`,
`completion.outcome`, `completion.status` and `system_error`.

## Verify or regenerate

Native tests cover C interoperability, owned values, concurrent calls/close,
timeout/reuse, move and initialization rollback. Python tests start fresh C peer
processes; stop the manually started service before testing:

```sh
WIRELINK_CALCULATOR_SERVER=/tmp/wirelink-calculator-sdk/tests/calculator_c_server \
  /tmp/wirelink-sdk-venv/bin/python -m pytest examples/06_bindings/tests
```

The pending-call test waits for a real outgoing datagram before close, checking
GIL release without a sleep-based admission guess. Capacity tests expect explicit
errors. `tests/check_wheel.py` in this example installs a wheel offline in a clean
venv and tests RPC/rejection against the C peer without build tools on PATH.

Rebuild matching WLC after modifying templates, then regenerate:

```sh
/absolute/path/to/matching-wlc sdk examples/06_bindings/schema/calculator.wl \
  --profile examples/06_bindings/schema/calculator.bind.wl \
  --out-dir examples/06_bindings --name calculator --package-version 0.13.0 --overwrite
python3 tests/package/check_generated_sdks.py --wlc /absolute/path/to/matching-wlc
```

The schema/profile match `examples/01_rpc`. Files listed in `wlc-sdk-manifest.json`
are generated; keep this GUIDE and business tests outside that inventory.
CMake requires Wirelink 0.13.0 and checks WLC version/codegen contract in the
included kernel manifest against the installed package metadata. Build metadata in `wlc-sdk-build.json` uses format
`wirelink-sdk-build-v2`, kernel contract 1; it is not a linker-layout control.

The [Device SDK](../07_device_sdk/README.md) exercises owned string/bytes, optional,
enum, packed-array and nested-message values. Generated SDKs provide UDP RPC clients;
subscriptions, Python handlers, Serial/USB, dynamic schemas, free-threaded Python
and subinterpreters are outside this interface. For a long job, define a separate
status-query RPC in the application protocol.
