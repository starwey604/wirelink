# Send telemetry with one owner

[中文](getting-started-cn.md) · [RPC](tutorial-rpc.md)

Send 12 telemetry messages, then close both endpoints. A successful run prints:

```text
12 ordinary telemetry messages received; both endpoints quiescent
```

Run from the Wirelink root with matching WLC 0.13.0 (contract 0.13); see [tool setup](development.md#build-matching-wlc-and-generated-tutorials). The C11 endpoints share one owner and a RAM-copy loopback, without hardware. Its tick is one loop pass, not a millisecond. Fixture instance IDs are process-local; production uses a fresh platform identity.

```sh
cmake -S . -B /tmp/wirelink-docs-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-tutorials --parallel \
  --target wirelink_telemetry_demo
ctest --test-dir /tmp/wirelink-docs-tutorials --output-on-failure \
  -R '^wirelink_telemetry_demo$'
/tmp/wirelink-docs-tutorials/examples/00_telemetry/wirelink_telemetry_demo
```

## Reuse the source after admission

`telemetry_kernel_send_telemetry` encodes a snapshot before `WL_OK`, so immediately clearing `message` below is safe. An occupied ordinary snapshot returns `WL_ERR_WOULD_BLOCK`: retain the value in the application and retry later.

[00_telemetry/publisher.c](../examples/00_telemetry/publisher.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_kernel.h"
#include "loopback_owner.h"
extern unsigned telemetry_received;
void telemetry_receive(void *, const telemetry_t *);
static telemetry_kernel_t endpoints[2];
static example_pair_t pair;
int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const telemetry_kernel_handlers_t handlers = {.on_message_telemetry = telemetry_receive};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(telemetry_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  for (unsigned sample = 0; sample < 12; ++sample) {
    telemetry_value_t message = {.has_sample = true, .sample = sample,
      .has_temperature_centi_c = true, .temperature_centi_c = -1234};
    EXAMPLE_REQUIRE(telemetry_kernel_send_telemetry(&endpoints[0], &message) == WL_OK);
    memset(&message, 0, sizeof(message)); /* Admission copied the source. */
    example_pair_wait(&pair, &telemetry_received, sample + 1);
  }
  example_pair_close(&pair);
  puts("12 ordinary telemetry messages received; both endpoints quiescent");
  return 0;
}
```

## Read or copy in the handler

The receive view is borrowed until callback return. Copy into application storage if retaining it; this handler only checks the sequence and temperature immediately.

[00_telemetry/subscriber.c](../examples/00_telemetry/subscriber.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_kernel.h"
#include "loopback_owner.h"
unsigned telemetry_received;
void telemetry_receive(void *user, const telemetry_t *message) {
  (void)user;
  EXAMPLE_REQUIRE(message->sample == telemetry_received);
  EXAMPLE_REQUIRE(message->temperature_centi_c == -1234);
  ++telemetry_received;
}
```

`example_pair_step` steps both kernels on the same owner, services loopback and increments its tick. `example_pair_close` closes both cores, stops retained transfers, then steps to quiescence. See the [execution model](kernel.md).
