/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <wirelink/bulk.h>
#include <wirelink/cobs.h>
#include <wirelink/codec.h>
#include <wirelink/crc.h>
#include <wirelink/fifo.h>
#include <wirelink/frame.h>
#include <wirelink/latest.h>
#include <wirelink/rpc.h>
#include <wirelink/span.h>
#include <wirelink/types.h>
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
_Static_assert(sizeof(wl_rpc_err_t) == sizeof(int32_t),
               "public RPC ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_rpc_client_state_t) == sizeof(int32_t),
               "public RPC state ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_rpc_cache_policy_t) == sizeof(int32_t),
               "public RPC cache ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_rpc_server_disposition_t) == sizeof(int32_t),
               "public RPC server ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_bulk_err_t) == sizeof(int32_t) &&
                   sizeof(wl_bulk_phase_t) == sizeof(int32_t) &&
                   sizeof(wl_bulk_status_code_t) == sizeof(int32_t) &&
                   sizeof(wl_bulk_sink_result_t) == sizeof(int32_t) &&
                   sizeof(wl_bulk_receiver_state_t) == sizeof(int32_t) &&
                   sizeof(wl_bulk_sender_state_t) == sizeof(int32_t),
               "public Bulk ABI must not depend on -fshort-enums");
_Static_assert(sizeof(wl_bulk_receiver_t) == WL_BULK_RECEIVER_STORAGE_SIZE &&
                   sizeof(wl_bulk_sender_t) == WL_BULK_SENDER_STORAGE_SIZE,
               "public Bulk opaque storage size changed");
_Static_assert(sizeof(wl_fifo_t) == WL_FIFO_CONTEXT_STORAGE_SIZE,
               "public FIFO context size changed");
_Static_assert(sizeof(wl_latest_t) == WL_LATEST_CONTEXT_STORAGE_SIZE,
               "public LATEST context size changed");
_Static_assert(sizeof(wl_rpc_client_t) == WL_RPC_CLIENT_STORAGE_SIZE &&
                   sizeof(wl_rpc_client_slot_t) ==
                       WL_RPC_CLIENT_SLOT_STORAGE_SIZE &&
                   sizeof(wl_rpc_server_t) == WL_RPC_SERVER_STORAGE_SIZE &&
                   sizeof(wl_rpc_server_pending_slot_t) ==
                       WL_RPC_SERVER_PENDING_SLOT_STORAGE_SIZE &&
                   sizeof(wl_rpc_server_cache_slot_t) ==
                       WL_RPC_SERVER_CACHE_SLOT_STORAGE_SIZE,
               "public RPC opaque storage size changed");
_Static_assert(sizeof(wl_poll_hint_t) == 8U &&
                   _Alignof(wl_poll_hint_t) == _Alignof(uint32_t) &&
                   offsetof(wl_poll_hint_t, work_pending) == 0U &&
                   offsetof(wl_poll_hint_t, next_deadline_ms) == 4U,
               "public poll hint ABI changed");

/*
 * The v1 layout baseline is architecture-specific. CI records the Linux
 * x86-64 SysV ABI explicitly; other targets still verify every fixed-width
 * enum-like public type above and exercise the installed package normally.
 */
#if defined(__linux__) && defined(__x86_64__) && UINTPTR_MAX == UINT64_MAX && \
    SIZE_MAX == UINT64_MAX
_Static_assert(sizeof(wl_ctx_t) == 896U && _Alignof(wl_ctx_t) == 16U,
               "v1 context ABI changed");

_Static_assert(sizeof(wl_span_t) == 16U && _Alignof(wl_span_t) == 8U &&
                   offsetof(wl_span_t, data) == 0U &&
                   offsetof(wl_span_t, length) == 8U,
               "v1 span ABI changed");
_Static_assert(sizeof(wl_codec_bytes_t) == 16U &&
                   _Alignof(wl_codec_bytes_t) == 8U &&
                   offsetof(wl_codec_bytes_t, data) == 0U &&
                   offsetof(wl_codec_bytes_t, length) == 8U,
               "v1 codec bytes ABI changed");
_Static_assert(sizeof(wl_codec_string_t) == 16U &&
                   _Alignof(wl_codec_string_t) == 8U &&
                   offsetof(wl_codec_string_t, data) == 0U &&
                   offsetof(wl_codec_string_t, length) == 8U,
               "v1 codec string ABI changed");

_Static_assert(sizeof(wl_frame_header_t) == 24U &&
                   _Alignof(wl_frame_header_t) == 8U &&
                   offsetof(wl_frame_header_t, marker_version_kind) == 0U &&
                   offsetof(wl_frame_header_t, reserved) == 1U &&
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
                   _Alignof(wl_wire_packet_t) == 8U &&
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
                   _Alignof(wl_frame_view_t) == 8U &&
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

_Static_assert(sizeof(wl_config_t) == 40U && _Alignof(wl_config_t) == 8U &&
                   offsetof(wl_config_t, max_payload_len) == 0U &&
                   offsetof(wl_config_t, envelope) == 4U &&
                   offsetof(wl_config_t, integrity) == 8U &&
                   offsetof(wl_config_t, session_id) == 16U &&
                   offsetof(wl_config_t, max_retries) == 24U &&
                   offsetof(wl_config_t, ack_timeout_ms) == 28U &&
                   offsetof(wl_config_t, max_transmission_unit) == 32U,
               "v1 config ABI changed");
_Static_assert(sizeof(wl_storage_t) == 80U && _Alignof(wl_storage_t) == 8U &&
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
                   _Alignof(wl_storage_requirements_t) == 8U &&
                   offsetof(wl_storage_requirements_t, tx_payload_size) == 0U &&
                   offsetof(wl_storage_requirements_t, tx_unit_size) == 8U &&
                   offsetof(wl_storage_requirements_t, control_unit_size) == 16U &&
                   offsetof(wl_storage_requirements_t, rx_fifo_size) == 24U &&
                   offsetof(wl_storage_requirements_t, rx_fallback_size) == 32U,
               "v1 storage requirements ABI changed");
_Static_assert(sizeof(wl_event_t) == 48U && _Alignof(wl_event_t) == 8U &&
                   offsetof(wl_event_t, type) == 0U &&
                   offsetof(wl_event_t, message_id) == 4U &&
                   offsetof(wl_event_t, payload) == 8U &&
                   offsetof(wl_event_t, payload_len) == 16U &&
                   offsetof(wl_event_t, handle) == 24U &&
                   offsetof(wl_event_t, io_result) == 28U &&
                   offsetof(wl_event_t, lease) == 32U &&
                   offsetof(wl_event_t, peer_session_id) == 40U,
               "v1 event ABI changed");
_Static_assert(sizeof(wl_rpc_request_identity_t) == 24U &&
                   _Alignof(wl_rpc_request_identity_t) == 8U &&
                   offsetof(wl_rpc_request_identity_t, operation_id) == 0U &&
                   offsetof(wl_rpc_request_identity_t, request_message_id) == 4U &&
                   offsetof(wl_rpc_request_identity_t, response_message_id) == 6U &&
                   offsetof(wl_rpc_request_identity_t, request_fingerprint) == 8U &&
                   offsetof(wl_rpc_request_identity_t, peer_session_id) == 16U,
               "v1 RPC request identity ABI changed");
_Static_assert(sizeof(wl_tx_result_t) == 12U &&
                   _Alignof(wl_tx_result_t) == 4U &&
                   offsetof(wl_tx_result_t, state) == 0U &&
                   offsetof(wl_tx_result_t, result) == 4U &&
                   offsetof(wl_tx_result_t, retries_used) == 8U,
               "v1 transaction result ABI changed");
_Static_assert(sizeof(wl_rx_counters_t) == 20U &&
                   _Alignof(wl_rx_counters_t) == 4U &&
                   offsetof(wl_rx_counters_t, malformed) == 0U &&
                   offsetof(wl_rx_counters_t, bad_integrity) == 4U &&
                   offsetof(wl_rx_counters_t, overflow) == 8U &&
                   offsetof(wl_rx_counters_t, duplicate) == 12U &&
                   offsetof(wl_rx_counters_t, unsupported) == 16U,
               "v1 RX counters ABI changed");
_Static_assert(sizeof(wl_rx_dma_claim_t) == 24U &&
                   _Alignof(wl_rx_dma_claim_t) == 8U &&
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
  wl_poll_hint_t poll_hint = {0};
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
  wl_latest_t latest = {0};
  uint32_t latest_slots[WL_LATEST_SLOT_COUNT] = {0};
  const wl_latest_config_t latest_config = {
      .value_size = sizeof(latest_slots[0]),
      .value_alignment = _Alignof(uint32_t),
  };
  const wl_latest_storage_t latest_storage = {
      .data = latest_slots,
      .size = sizeof(latest_slots),
  };
  wl_latest_write_claim_t latest_claim = {0};
  wl_latest_view_t latest_view = {0};
  wl_fifo_t fifo = {0};
  uint32_t fifo_slots[2] = {0};
  const wl_fifo_config_t fifo_config = {
      .value_size = sizeof(fifo_slots[0]),
      .value_alignment = _Alignof(uint32_t),
      .capacity = 2U,
  };
  const wl_fifo_storage_t fifo_storage = {
      .data = fifo_slots,
      .size = sizeof(fifo_slots),
  };
  wl_fifo_write_claim_t fifo_claim = {0};
  wl_fifo_view_t fifo_view = {0};
  wl_rpc_client_t rpc_client = {0};
  wl_rpc_client_slot_t rpc_slots[1] = {0};
  uint8_t rpc_responses[8] = {0};
  const wl_rpc_client_config_t rpc_config = {
      .slots = rpc_slots,
      .slot_count = 1U,
      .response_storage = rpc_responses,
      .response_storage_size = sizeof(rpc_responses),
      .response_capacity_per_slot = sizeof(rpc_responses),
  };
  uint32_t operation_id = 0U;

  if (WIRELINK_PROTOCOL_VERSION != WL_FRAME_VERSION ||
      wl_config_requirements(&config, &requirements) != WL_OK ||
      requirements.rx_fifo_size != 0U ||
      wl_init(&context, &config, &storage) != WL_OK ||
      wl_get_config(&context, &observed) != WL_OK ||
      wl_poll_get_hint(&context, 0U, &poll_hint) != WL_OK ||
      poll_hint.work_pending != 0U ||
      poll_hint.next_deadline_ms != WL_POLL_NO_DEADLINE_MS ||
      observed.max_payload_len != config.max_payload_len ||
      observed.envelope != config.envelope ||
      observed.integrity != config.integrity ||
      observed.session_id != config.session_id ||
      observed.max_retries != config.max_retries ||
      observed.ack_timeout_ms != config.ack_timeout_ms ||
      observed.max_transmission_unit != config.max_transmission_unit ||
      wl_latest_init(&latest, &latest_config, &latest_storage) != WL_OK ||
      wl_latest_write_claim(&latest, &latest_claim) != WL_OK ||
      wl_fifo_init(&fifo, &fifo_config, &fifo_storage) != WL_OK ||
      wl_fifo_write_claim(&fifo, &fifo_claim) != WL_OK) {
    return 1;
  }

  *(uint32_t *)latest_claim.value = UINT32_C(0x574c);   /* "WL" */
  *(uint32_t *)fifo_claim.value = UINT32_C(0x4649464f); /* "FIFO" */
  if (wl_latest_write_publish(&latest, &latest_claim) != WL_OK ||
      wl_latest_read_acquire(&latest, &latest_view) != WL_OK ||
      *(const uint32_t *)latest_view.value != UINT32_C(0x574c) ||
      wl_latest_read_release(&latest, &latest_view) != WL_OK ||
      wl_fifo_write_publish(&fifo, &fifo_claim) != WL_OK ||
      wl_fifo_read_acquire(&fifo, &fifo_view) != WL_OK ||
      *(const uint32_t *)fifo_view.value != UINT32_C(0x4649464f) ||
      wl_fifo_read_release(&fifo, &fifo_view) != WL_OK ||
      wl_rpc_client_init(&rpc_client, &rpc_config) != WL_RPC_OK ||
      wl_rpc_client_begin(&rpc_client, 1U, 2U, 10U, 0U, &operation_id) !=
          WL_RPC_OK ||
      operation_id == 0U) {
    return 1;
  }
  return 0;
}
