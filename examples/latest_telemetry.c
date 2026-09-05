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
