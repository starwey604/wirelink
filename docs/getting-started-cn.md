# 单 owner 遥测

普通遥测进入借用的 typed callback。本例发送 12 条消息，接纳后立即清空源值。框架不保留 LATEST mailbox，跨 callback 保存的值由应用自行复制。

本页对应当前 WLC/codegen 0.13 和 profile v2。源码为 C11，两个端点在同一应用 owner 上通过真实 RAM copy 连接，不打开硬件。示例 helper 的递增 instance 仅用于进程内测试；生产使用平台随机源或持久唯一分配器。

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
cmake --build build/tutorials --parallel
ctest --test-dir build/tutorials --output-on-failure
```

运行目标 `wirelink_telemetry_demo` （位于对应 examples 子目录）。

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

[内核 API](static-endpoint-kernel-api.md) · [应用长任务](tutorial-rpc-deferred-cn.md)
