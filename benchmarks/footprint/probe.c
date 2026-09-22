/* SPDX-License-Identifier: Apache-2.0 */

/* Static-footprint probe. It links against the core objects and prints the
 * caller-provided storage the library asks for, plus the size of the public
 * structs, in a stable CSV that benchmarks/footprint/footprint.py parses. */

#include <stdint.h>
#include <stdio.h>

#include "wirelink/frame.h"
#include "wirelink/wirelink.h"

#define PROBE_PREFIX "wirelink_footprint_v1"

static void print_type(const char *name, size_t size) {
  printf(PROBE_PREFIX ",type,%s,%u\n", name, (unsigned int)size);
}

static void print_config(wl_envelope_type_t envelope, wl_integrity_t integrity,
                         uint16_t max_payload) {
  wl_config_t config = {
      .max_payload_len = max_payload,
      .envelope = envelope,
      .integrity = integrity,
      .session_id = UINT64_C(1),
      .max_retries = 1U,
      .ack_timeout_ms = 100U,
      .max_transmission_unit = WL_FRAME_MAX_COBS_LEN,
  };
  wl_storage_requirements_t requirements = {0};

  if (wl_config_requirements(&config, &requirements) != WL_OK) {
    return;
  }
  /* envelope, integrity, max_payload, mtu, tx_payload, tx_unit, control,
   * rx_fifo, rx_fallback. */
  printf(PROBE_PREFIX ",storage,%d,%d,%u,%u,%u,%u,%u,%u,%u\n", (int)envelope,
         (int)integrity, (unsigned int)max_payload,
         (unsigned int)WL_FRAME_MAX_COBS_LEN,
         (unsigned int)requirements.tx_payload_size,
         (unsigned int)requirements.tx_unit_size,
         (unsigned int)requirements.control_unit_size,
         (unsigned int)requirements.rx_fifo_size,
         (unsigned int)requirements.rx_fallback_size);
}

int main(void) {
  static const uint16_t payloads[] = {20U, 120U, 2048U};
  static const wl_integrity_t integrities[] = {WL_INTEGRITY_NONE,
                                               WL_INTEGRITY_CRC32C};

  printf(PROBE_PREFIX ",probe,1\n");
  print_type("wl_ctx_t", sizeof(wl_ctx_t));
  print_type("wl_event_t", sizeof(wl_event_t));
  print_type("wl_config_t", sizeof(wl_config_t));
  print_type("wl_storage_t", sizeof(wl_storage_t));
  print_type("wl_storage_requirements_t", sizeof(wl_storage_requirements_t));

  for (size_t i = 0U; i < sizeof(integrities) / sizeof(integrities[0]); ++i) {
    for (size_t j = 0U; j < sizeof(payloads) / sizeof(payloads[0]); ++j) {
      print_config(WL_ENVELOPE_COBS_STREAM, integrities[i], payloads[j]);
    }
  }
  /* The non-COBS envelopes keep no RX ring, which shows up as rx_fifo == 0. */
  print_config(WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC32C, 2048U);
  return 0;
}
