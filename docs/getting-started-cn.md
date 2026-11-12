# 用一个 owner 发送遥测

[English](getting-started.md) · [RPC](tutorial-rpc-cn.md)

发送 12 条遥测消息，然后关闭两个端点。成功运行会输出：

```text
12 ordinary telemetry messages received; both endpoints quiescent
```

在 Wirelink 根目录运行，准备匹配的 WLC 0.13.0（contract 0.13），见[工具配置](development-cn.md#构建匹配-wlc-和生成教程)。C11 端点共享一个 owner 和 RAM-copy loopback，不打开硬件。一个 tick 是一次循环，不是毫秒。fixture instance ID 仅限进程内使用，生产环境使用新的平台身份。

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

## 接纳后复用发送值

`telemetry_kernel_send_telemetry` 在返回 `WL_OK` 前编码 snapshot，所以下文立即清空 `message` 是安全的。普通 snapshot 忙时返回 `WL_ERR_WOULD_BLOCK`，消息尚未接纳，由应用保留值并稍后重试。

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

## 在 handler 内读取或复制

接收 view 只借用到 callback 返回；需要保留时复制到应用存储。本例只立即验证 sequence 和 temperature。

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

`example_pair_step` 在同一 owner 上 step 两个 kernel，再 service loopback 并递增 tick。`example_pair_close` close 两端、stop 实际保留传输，再 step 到 quiescence。见[执行模型](kernel-cn.md)。
