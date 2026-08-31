/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>
#include <wirelink/version.h>
#include <wirelink/wirelink.h>

_Static_assert(sizeof(wl_envelope_type_t) == sizeof(int32_t),
               "public envelope ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_integrity_t) == sizeof(int32_t),
               "public integrity ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_packet_type_t) == sizeof(int32_t),
               "public packet ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_packet_flag_t) == sizeof(int32_t),
               "public packet flag ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_err_t) == sizeof(int32_t),
               "public error ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_tx_state_t) == sizeof(int32_t),
               "public transaction ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_tx_wait_reason_t) == sizeof(int32_t),
               "public wait-reason ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_event_type_t) == sizeof(int32_t),
               "public event ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_sink_result_t) == sizeof(int32_t),
               "public sink ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_codec_status_t) == sizeof(int32_t),
               "public codec ABI must not depend on -fshort-enums");

/*
 * The v1 layout baseline is architecture-specific. CI records the LP64 Linux
 * ABI explicitly; other targets still verify every fixed-width enum-like
 * public type above and exercise the installed package normally.
 */
#if defined(__linux__) && UINTPTR_MAX == UINT64_MAX && SIZE_MAX == UINT64_MAX
_Static_assert(sizeof(wl_ctx_t) == 640U && _Alignof(wl_ctx_t) == 16U,
               "v1 context ABI changed");

_Static_assert(sizeof(wl_span_t) == 16U &&
                   offsetof(wl_span_t, data) == 0U &&
                   offsetof(wl_span_t, length) == 8U,
               "v1 span ABI changed");
_Static_assert(sizeof(wl_codec_bytes_t) == 16U &&
                   offsetof(wl_codec_bytes_t, data) == 0U &&
                   offsetof(wl_codec_bytes_t, length) == 8U,
               "v1 codec bytes ABI changed");
_Static_assert(sizeof(wl_codec_string_t) == 16U &&
                   offsetof(wl_codec_string_t, data) == 0U &&
                   offsetof(wl_codec_string_t, length) == 8U,
               "v1 codec string ABI changed");

_Static_assert(sizeof(wl_frame_header_t) == 24U &&
                   offsetof(wl_frame_header_t, magic) == 0U &&
                   offsetof(wl_frame_header_t, version) == 2U &&
                   offsetof(wl_frame_header_t, header_length) == 3U &&
                   offsetof(wl_frame_header_t, packet_type) == 4U &&
                   offsetof(wl_frame_header_t, flags) == 5U &&
                   offsetof(wl_frame_header_t, session_id) == 8U &&
                   offsetof(wl_frame_header_t, sequence) == 16U &&
                   offsetof(wl_frame_header_t, message_id) == 20U &&
                   offsetof(wl_frame_header_t, payload_length) == 22U,
               "v1 frame header ABI changed");
_Static_assert(sizeof(wl_wire_packet_t) == 48U &&
                   offsetof(wl_wire_packet_t, type) == 0U &&
                   offsetof(wl_wire_packet_t, integrity) == 4U &&
                   offsetof(wl_wire_packet_t, flags) == 8U &&
                   offsetof(wl_wire_packet_t, message_id) == 10U &&
                   offsetof(wl_wire_packet_t, session_id) == 16U &&
                   offsetof(wl_wire_packet_t, sequence) == 24U &&
                   offsetof(wl_wire_packet_t, payload) == 32U &&
                   offsetof(wl_wire_packet_t, payload_len) == 40U,
               "v1 wire packet ABI changed");
_Static_assert(sizeof(wl_frame_view_t) == 64U &&
                   offsetof(wl_frame_view_t, type) == 0U &&
                   offsetof(wl_frame_view_t, flags) == 4U &&
                   offsetof(wl_frame_view_t, message_id) == 6U &&
                   offsetof(wl_frame_view_t, session_id) == 8U &&
                   offsetof(wl_frame_view_t, sequence) == 16U &&
                   offsetof(wl_frame_view_t, integrity) == 20U &&
                   offsetof(wl_frame_view_t, payload) == 24U &&
                   offsetof(wl_frame_view_t, integrity_bytes) == 40U &&
                   offsetof(wl_frame_view_t, total_len) == 56U,
               "v1 frame view ABI changed");

_Static_assert(sizeof(wl_config_t) == 40U &&
                   offsetof(wl_config_t, max_payload_len) == 0U &&
                   offsetof(wl_config_t, envelope) == 4U &&
                   offsetof(wl_config_t, integrity) == 8U &&
                   offsetof(wl_config_t, session_id) == 16U &&
                   offsetof(wl_config_t, max_retries) == 24U &&
                   offsetof(wl_config_t, ack_timeout_ms) == 28U &&
                   offsetof(wl_config_t, max_transmission_unit) == 32U,
               "v1 config ABI changed");
_Static_assert(sizeof(wl_storage_t) == 80U &&
                   offsetof(wl_storage_t, tx_payload) == 0U &&
                   offsetof(wl_storage_t, tx_payload_size) == 8U &&
                   offsetof(wl_storage_t, tx_unit) == 16U &&
                   offsetof(wl_storage_t, tx_unit_size) == 24U &&
                   offsetof(wl_storage_t, control_unit) == 32U &&
                   offsetof(wl_storage_t, control_unit_size) == 40U &&
                   offsetof(wl_storage_t, rx_fifo) == 48U &&
                   offsetof(wl_storage_t, rx_fifo_size) == 56U &&
                   offsetof(wl_storage_t, rx_fallback) == 64U &&
                   offsetof(wl_storage_t, rx_fallback_size) == 72U,
               "v1 storage ABI changed");
_Static_assert(sizeof(wl_storage_requirements_t) == 40U &&
                   offsetof(wl_storage_requirements_t, tx_payload_size) == 0U &&
                   offsetof(wl_storage_requirements_t, tx_unit_size) == 8U &&
                   offsetof(wl_storage_requirements_t, control_unit_size) == 16U &&
                   offsetof(wl_storage_requirements_t, rx_fifo_size) == 24U &&
                   offsetof(wl_storage_requirements_t, rx_fallback_size) == 32U,
               "v1 storage requirements ABI changed");
_Static_assert(sizeof(wl_event_t) == 40U &&
                   offsetof(wl_event_t, type) == 0U &&
                   offsetof(wl_event_t, message_id) == 4U &&
                   offsetof(wl_event_t, payload) == 8U &&
                   offsetof(wl_event_t, payload_len) == 16U &&
                   offsetof(wl_event_t, handle) == 24U &&
                   offsetof(wl_event_t, io_result) == 28U &&
                   offsetof(wl_event_t, lease) == 32U,
               "v1 event ABI changed");
_Static_assert(sizeof(wl_tx_result_t) == 12U &&
                   offsetof(wl_tx_result_t, state) == 0U &&
                   offsetof(wl_tx_result_t, result) == 4U &&
                   offsetof(wl_tx_result_t, retries_used) == 8U,
               "v1 transaction result ABI changed");
_Static_assert(sizeof(wl_rx_counters_t) == 20U &&
                   offsetof(wl_rx_counters_t, malformed) == 0U &&
                   offsetof(wl_rx_counters_t, bad_integrity) == 4U &&
                   offsetof(wl_rx_counters_t, overflow) == 8U &&
                   offsetof(wl_rx_counters_t, duplicate) == 12U &&
                   offsetof(wl_rx_counters_t, unsupported) == 16U,
               "v1 RX counters ABI changed");
_Static_assert(sizeof(wl_rx_dma_claim_t) == 24U &&
                   offsetof(wl_rx_dma_claim_t, span) == 0U &&
                   offsetof(wl_rx_dma_claim_t, token) == 16U,
               "v1 DMA claim ABI changed");
#endif

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
