# Getting started: display the latest temperature

Imagine a device measuring temperature and a display showing its current reading.
The display does not need to show every historical measurement. This tutorial
implements that application with Wirelink.

No board is needed. One desktop program contains both the sensor and display;
an in-memory connection transfers their messages. Wirelink encodes a temperature
structure and reconstructs it at the other end. Your program chooses when to
send and when to refresh the display.

This is lesson one. Continue with [requesting a calculation](tutorial-rpc.md),
then [using your own project and hardware](tutorial-integration.md).
No API or protocol reference is prerequisite. [中文](getting-started-cn.md).

## 1. Run it first

First follow [environment setup: install WLC](installation.md). Running the example
requires a C11 compiler, CMake 3.21 or newer, and a matching `wlc` on your PATH.
There is no requirement for WLC sources inside the Wirelink checkout.
Run these commands at the Wirelink root:

```sh
cmake -S . -B build/quickstart \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
cmake --build build/quickstart --target wirelink_latest_telemetry
./build/quickstart/examples/wirelink_latest_telemetry
```

Expected output:

```text
latest telemetry: sample=2 temperature=23.50 C
```

The program sends sample 1 at 23.00 °C and sample 2 at 23.50 °C.
Both arrive before the display reads, so it gets only sample 2.
Next we explain the definitions, reception policy, and complete program.

## 2. Describe a temperature message

[`temperature.wl`](../examples/getting_started/temperature.wl):

```text
version 1;

message Telemetry = 10 {
  required uint32 sample = 1;
  required int32 temperature_centi_c = 2;
}
```

`Telemetry` is our message name, not a Wirelink keyword. Its fields are a sample
number and a temperature in hundredths of a degree Celsius: 2350 means 23.50 °C.
Integers keep floating-point conversion out of the message definition.

The 10 identifies the message type. The 1 and 2 identify fields within that
message; they are not default values. `required` means the field must be present.
`uint32` and `int32` are unsigned and signed 32-bit integers. `version 1`
selects the definition language version.

This file is a **schema**, a description of the messages. Generating both ends
from the same schema gives them matching encoding rules; we do not transmit
the raw memory layout of a C struct.

## 3. Describe what happens after reception

[`temperature.bind.wl`](../examples/getting_started/temperature.bind.wl):

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}
```

These are two independent choices:

| Setting | Question | Choice in this example |
| --- | --- | --- |
| `latest Telemetry` | What if another value arrives before the application reads? | Replace the unread value with the most recently received value |
| `delivery = unreliable` | Which delivery mode does this receive path accept? | No acknowledgement or retransmission |

LATEST is a storage policy. Unreliable is a delivery mode. We combine them
because another temperature measurement will soon replace a lost one.
LATEST can also retain reliable messages; unreliable messages need not use LATEST.

“Latest” means last received and published into this storage, not greatest
`sample` or timestamp. Wirelink does not compare those fields. Applications
using a transport that reorders packets must decide how to reject old samples.

The `bind` in `.bind.wl` is a filename convention for a **binding profile**:
a configuration binding messages to handling policies. It uses its own syntax,
starting with `profile version 1;`. It is not a second message definition.
The suffix is not mandatory; the generator selects this file through its
`PROFILE` argument.

Separating the files lets a sensor send, a display retain the newest value,
and a recorder process received values in order, using a shared message schema
and different handling configurations. Encoding/decoding alone needs no
`.bind.wl`; we add it to generate the LATEST receive API.

The endpoint's `send_telemetry()` follows this configured delivery mode, so ordinary
sending does not repeat the choice. Advanced codec sends still accept an explicit mode.

## 4. Only three communication objects

WLC generates `temperature_endpoint_t` from the schema and binding profile.
It includes link state, message handling, and the required static buffers.
You neither define its structure nor guess buffer sizes.

`device` and `display` are two instances, not network addresses. `static`
zero-initializes them and keeps them alive throughout the program. Do not copy
or move an initialized endpoint.

`cable` is the simulated connection, later replaceable with a hardware adapter.
`endpoint_handle()` supplies the common endpoint interface used by adapters,
including connections between differently generated endpoint types.

The 1 and 2 are nonzero session identifiers for this isolated simulation.
Reboot handling for reliable communication is introduced in lesson two.

## 5. Complete C program

This is [`examples/latest_telemetry.c`](../examples/latest_telemetry.c), with no
hidden initializer. `CHECK` prints the failing expression's line and exits
this desktop example; it is not a Wirelink API.
The generated `temperature_runtime.h` also declares the default endpoint interface.

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>

#include "temperature_runtime.h"
#include "wirelink/loopback.h"

/* Desktop example: print the failing expression and stop on unexpected errors. */
#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
    return 1; \
  } \
} while (0)

int main(void) {
  static temperature_endpoint_t device, display;
  wl_loopback_t cable;
  telemetry_t received;

  /* Fixed IDs are only for this isolated simulation. */
  CHECK(temperature_endpoint_init(&device, 1U) == WL_OK);
  CHECK(temperature_endpoint_init(&display, 2U) == WL_OK);
  CHECK(wl_loopback_connect(&cable, temperature_endpoint_handle(&device),
                           temperature_endpoint_handle(&display)) == WL_OK);

  for (uint32_t sample = 1U; sample <= 2U; ++sample) {
    telemetry_t message;
    telemetry_clear(&message);
    message.has_sample = true;
    message.sample = sample;
    message.has_temperature_centi_c = true;
    message.temperature_centi_c = sample == 1U ? 2300 : 2350;

    CHECK(temperature_endpoint_send_telemetry(&device, &message).domain
          == TEMPERATURE_SEND_OK);
    /* Each endpoint advances transport and message handling in one call. */
    CHECK(temperature_endpoint_step(&device, sample) == WL_OK);
    CHECK(temperature_endpoint_step(&display, sample) == WL_OK);
  }

  CHECK(temperature_endpoint_read_telemetry(&display, &received) == WL_OK);
  CHECK(received.sample == 2U && received.temperature_centi_c == 2350);
  printf("latest telemetry: sample=%u temperature=%.2f C\n",
         (unsigned)received.sample, received.temperature_centi_c / 100.0);
  CHECK(temperature_endpoint_read_telemetry(&display, &received) == WL_ERR_NO_DATA);

  temperature_endpoint_close(&device);
  temperature_endpoint_close(&display);
  return 0;
}
```

## 6. Follow one measurement

Clear the message, then set values and their `has_...` presence flags.
Zero is valid data, so cannot stand for a missing field.

`temperature_endpoint_send_telemetry()` encodes and submits using the delivery
mode from `.bind.wl`. `TEMPERATURE_SEND_OK` means local acceptance, not reception.

Wirelink creates no thread. Calling each endpoint's `endpoint_step()` advances
transport, handles incoming messages, and reclaims send completions.
Here `sample` also acts as a manually advanced millisecond clock. Real applications
supply a monotonic clock and keep driving endpoints from one owner thread or loop.

`endpoint_read_telemetry()` copies the newest value into your `received` variable.
You own that copy and may retain or modify it; no pointer lease needs releasing.
Without a new value, it returns `WL_ERR_NO_DATA` and leaves the output unchanged.
That is not a communication failure.

Each step has a work budget, so it need not drain an arbitrary queue in one call.
One message per iteration makes stepping both ends sufficient here.
Close both endpoints with `endpoint_close()`; attached adapters stop accessing
storage. Keep a shared cable alive until both endpoints close.
Unexpected failures exit this desktop process; long-running applications also
close endpoints on error paths.

## Next

Change the second temperature to 2410 and rebuild to display 24.10 °C.
Move reading inside the loop, after stepping the display, to observe both samples.

Continue with [requesting a calculation](tutorial-rpc.md) for commands and results.
Read [integration](tutorial-integration.md) when changing memory layout, using DMA,
or customizing scheduling; those are not prerequisites for this example.
