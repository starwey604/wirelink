# Getting started: display the latest temperature

A sensor produces readings; a display wants the current temperature, not a replay
of every historical reading. Two independent programs implement this:
`telemetry_publisher` sends readings and `telemetry_subscriber` displays them.
No board is required. They use localhost UDP, a packet transport that does not
guarantee delivery.

This is [example 00_telemetry](../examples/00_telemetry/). Continue with
[RPC addition](tutorial-rpc.md), then [integration](tutorial-integration.md).
[中文](getting-started-cn.md).

## 1. Run both programs

Complete [installation](installation.md), including matching WLC and standalone Asio.
From the Wirelink root, substitute your own Asio include path:

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DWIRELINK_BUILD_GETTING_STARTED=ON -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio-source/asio/include
cmake --build build/tutorials --config Release --parallel
```

Start the receiver in terminal A:

```sh
./build/tutorials/examples/00_telemetry/telemetry_subscriber
```

After `telemetry subscriber ready`, start the sender in terminal B within ten seconds:

```sh
./build/tutorials/examples/00_telemetry/telemetry_publisher
```

The publisher sends five samples, 200 ms apart. Typical receiver output is:

```text
latest sample=1 temperature=23.50 C
latest sample=2 temperature=23.50 C
latest sample=3 temperature=23.50 C
latest sample=4 temperature=23.50 C
latest sample=5 temperature=23.50 C
```

Every sample need not appear. The receiver exits after sample 5, or fails if it
has not received it within ten seconds. With Visual Studio multi-configuration
builds, use `build/tutorials/examples/00_telemetry/Release/telemetry_subscriber.exe`
and the equivalent publisher path. No virtual serial driver or Python relay is needed.

## 2. Define the data

Complete [telemetry.wl](../examples/00_telemetry/telemetry.wl):

```text
version 1;

message Telemetry @id(10) {
  required uint32 sample @id(1);
  required int32 temperature_centi_c @id(2);
}
```

`Telemetry` is the application's message name, not a keyword. `sample` numbers
readings; temperature uses hundredths of a degree Celsius: 2350 means 23.50 °C.
`uint32` and `int32` are unsigned/signed 32-bit integers; `required` means a field
must be present. `@id(10)` identifies the message type; `@id(1)` and `@id(2)`
identify fields, not default values. Keep existing IDs when reordering fields.
`version 1` is this schema's first revision.

A **schema** defines the messages both peers encode and decode. It does not send
the C structure's memory layout directly.

## 3. Choose how the message is used

Complete [telemetry.bind.wl](../examples/00_telemetry/telemetry.bind.wl):

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}
```

These are independent choices:

- `latest Telemetry`: keep the last received value, replacing an unread older value.
- `delivery = unreliable`: do not wait for link acknowledgements or retransmit loss.

**Latest is not another name for unreliable.** One selects storage after receipt;
the other selects delivery. Regularly refreshed temperatures can tolerate missing
a sample. Latest means last received, not greatest `sample`: Wirelink does not
filter reordered older measurements for you.

The separate **binding profile** describes message use, not another data structure.
The `.bind.wl` suffix is a convention; CMake's `PROFILE` selects the actual file.
Different consumers can share a schema while retaining messages differently.
Default endpoint sends use the delivery selected here.

## 4. Distinguish generated APIs from example support

WLC generates `telemetry_endpoint_t`, including communication state and bounded
static storage. Each process owns one endpoint; no hand-written buffer assembly
is needed. The business name comes from `telemetry.wl`, not the directory's `00`.

`telemetry_runtime.h` is generated. [tutorial_host.h](../examples/common/tutorial_host.h)
is ordinary, public example support, not a generated file or core API:

- `example_udp_open()` attaches localhost UDP to the initialized endpoint.
- `example_now_ms()` supplies monotonic milliseconds; `example_session_id()`
  obtains a nonzero random identifier for this run.
- `example_udp_wait()` waits for socket readiness or the nearest deadline;
  it never dispatches business callbacks or creates a communication thread.
- `example_udp_close()` closes the endpoint and frees desktop resources.
  `CHECK` only reports an unexpected result and exits the example.

The [C++ implementation](../examples/common/tutorial_host.cpp) isolates platform
support and uses Wirelink's Asio UDP adapter. The business programs remain C11;
Python does not implement either peer.

## 5. Complete publisher

[publisher.c](../examples/00_telemetry/publisher.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_runtime.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static telemetry_endpoint_t publisher;
  uint16_t local = 49000, peer = 49001;
  telemetry_t value;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(telemetry_endpoint_init(&publisher, example_session_id()) == WL_OK);
  example_udp_t *udp = example_udp_open(telemetry_endpoint_handle(&publisher), local, peer);
  CHECK(udp != NULL);

  telemetry_clear(&value);
  value.has_sample = true;
  value.has_temperature_centi_c = true;
  value.temperature_centi_c = 2350;
  for (value.sample = 1; value.sample <= 5 && example_running(); ++value.sample) {
    CHECK(telemetry_endpoint_send_telemetry(&publisher, &value).domain == TELEMETRY_SEND_OK);
    printf("published sample=%u temperature=23.50 C\n", (unsigned)value.sample);
    /* Publish every 200 ms; keep servicing the endpoint while waiting. */
    const wl_time_ms_t started = example_now_ms();
    while ((wl_time_ms_t)(example_now_ms() - started) < 200U && example_running()) {
      CHECK(telemetry_endpoint_step(&publisher, example_now_ms()) == WL_OK);
      const uint32_t elapsed = example_now_ms() - started;
      if (elapsed < 200U) CHECK(example_udp_wait(udp, 200U - elapsed) == WL_OK);
    }
  }
  example_udp_close(udp);
  return 0;
}
```

`telemetry_clear()` initializes the message; `has_...` flags mark fields present,
since zero is also a valid value. A successful send means local submission,
not remote receipt. During each interval, step the endpoint before waiting.

## 6. Complete subscriber

[subscriber.c](../examples/00_telemetry/subscriber.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_runtime.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static telemetry_endpoint_t subscriber;
  uint16_t local = 49001, peer = 49000;
  telemetry_t value;
  int complete = 0;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(telemetry_endpoint_init(&subscriber, example_session_id()) == WL_OK);
  example_udp_t *udp = example_udp_open(telemetry_endpoint_handle(&subscriber), local, peer);
  CHECK(udp != NULL);
  puts("telemetry subscriber ready");
  fflush(stdout);

  const wl_time_ms_t started = example_now_ms();
  while (example_running() && (wl_time_ms_t)(example_now_ms() - started) < 10000U) {
    CHECK(telemetry_endpoint_step(&subscriber, example_now_ms()) == WL_OK);
    const int result = telemetry_endpoint_read_telemetry(&subscriber, &value);
    if (result == WL_OK) {
      printf("latest sample=%u temperature=%.2f C\n", (unsigned)value.sample,
             value.temperature_centi_c / 100.0);
      if (value.sample >= 5) { complete = 1; break; }
    } else {
      CHECK(result == WL_ERR_NO_DATA);
    }
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  example_udp_close(udp);
  if (!complete) fputs("no final sample received (UDP telemetry may be lost)\n", stderr);
  return complete ? 0 : 1;
}
```

`endpoint_step()` handles incoming messages. `read_telemetry()` copies the latest
value into application-owned storage; no borrowed pointer must be returned.
`WL_ERR_NO_DATA` means no new value, not a connection failure.
Each owner pass has a bounded work budget.

The programs bind ports 49000/49001 and accept only their configured peer.
To avoid occupied ports, append `49010 49011` to the publisher command and
`49011 49010` to the subscriber command. UDP ports are OS addresses, not
Wirelink session identifiers; reboot rules are deferred to integration.

## Next steps

Change the publisher's temperature to 2410 and rebuild; the display should show
24.10 °C. This is neither a reliable-delivery demonstration nor a performance
benchmark. Former single-process examples remain under
[tests/tutorials/loopback](../tests/tutorials/loopback/) for deterministic regression.

Continue with [RPC addition](tutorial-rpc.md), or consult
[integration](tutorial-integration.md) when replacing UDP with real serial I/O.
