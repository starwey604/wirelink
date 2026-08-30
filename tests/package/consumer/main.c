/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <wirelink/version.h>
#include <wirelink/wirelink.h>

int main(void) {
  wl_ctx_t context = {0};
  wl_config_t config = {
      .max_payload_len = 32U,
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0x12345678),
      .max_retries = 1U,
      .ack_timeout_ms = 10U,
  };
  wl_config_t observed = {0};
  wl_storage_requirements_t requirements = {0};
  uint8_t tx_payload[32];
  uint8_t tx_unit[WL_FRAME_MAX_RAW_LEN];
  uint8_t control_unit[WL_FRAME_MAX_RAW_LEN];
  uint8_t rx_fallback[WL_FRAME_MAX_RAW_LEN];
  wl_storage_t storage = {
      .tx_payload = tx_payload,
      .tx_payload_size = sizeof(tx_payload),
      .tx_unit = tx_unit,
      .tx_unit_size = sizeof(tx_unit),
      .control_unit = control_unit,
      .control_unit_size = sizeof(control_unit),
      .rx_fallback = rx_fallback,
      .rx_fallback_size = sizeof(rx_fallback),
  };

  if (WIRELINK_PROTOCOL_VERSION != WL_FRAME_VERSION ||
      wl_config_requirements(&config, &requirements) != WL_OK ||
      requirements.rx_fifo_size != 0U ||
      wl_init(&context, &config, &storage) != WL_OK ||
      wl_get_config(&context, &observed) != WL_OK ||
      observed.max_payload_len != config.max_payload_len ||
      observed.envelope != config.envelope ||
      observed.integrity != config.integrity ||
      observed.session_id != config.session_id ||
      observed.max_retries != config.max_retries ||
      observed.ack_timeout_ms != config.ack_timeout_ms ||
      observed.max_transmission_unit != config.max_transmission_unit) {
    return 1;
  }
  return 0;
}
