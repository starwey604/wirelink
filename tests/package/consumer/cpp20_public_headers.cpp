/* SPDX-License-Identifier: Apache-2.0 */

/* Keep this list aligned with the public headers installed by Wirelink. */
#include <wirelink/bulk.h>
#include <wirelink/cobs.h>
#include <wirelink/codec.h>
#include <wirelink/crc.h>
#include <wirelink/fifo.h>
#include <wirelink/frame.h>
#include <wirelink/latest.h>
#include <wirelink/link.h>
#include <wirelink/port.h>
#include <wirelink/profile.h>
#include <wirelink/rpc.h>
#include <wirelink/span.h>
#include <wirelink/types.h>
#include <wirelink/version.h>
#include <wirelink/wirelink.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(__cplusplus >= 202002L);
static_assert(std::is_standard_layout_v<wl_codec_bytes_t>);
static_assert(std::is_standard_layout_v<wl_frame_view_t>);
static_assert(std::is_standard_layout_v<wl_event_t>);
static_assert(std::is_standard_layout_v<wl_fifo_t>);
static_assert(std::is_standard_layout_v<wl_fifo_view_t>);
static_assert(std::is_standard_layout_v<wl_latest_t>);
static_assert(std::is_standard_layout_v<wl_latest_view_t>);
static_assert(std::is_standard_layout_v<wl_rpc_client_t>);
static_assert(std::is_standard_layout_v<wl_rpc_client_result_t>);
static_assert(std::is_standard_layout_v<wl_bulk_receiver_t>);
static_assert(std::is_standard_layout_v<wl_bulk_sender_t>);
static_assert(std::is_standard_layout_v<wl_bulk_status_t>);
static_assert(std::is_standard_layout_v<wl_poll_hint_t>);
static_assert(sizeof(wl_poll_hint_t) == 8U);
static_assert(alignof(wl_poll_hint_t) == alignof(std::uint32_t));
static_assert(sizeof(wl_codec_status_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_err_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_rpc_err_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_rpc_client_state_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_rpc_cache_policy_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_rpc_server_disposition_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_bulk_err_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_bulk_phase_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_bulk_status_code_t) == sizeof(std::int32_t));
static_assert(sizeof(wl_bulk_receiver_t) == WL_BULK_RECEIVER_STORAGE_SIZE);
static_assert(sizeof(wl_bulk_sender_t) == WL_BULK_SENDER_STORAGE_SIZE);
static_assert(sizeof(wl_fifo_t) == WL_FIFO_CONTEXT_STORAGE_SIZE);
static_assert(sizeof(wl_latest_t) == WL_LATEST_CONTEXT_STORAGE_SIZE);
static_assert(sizeof(wl_rpc_client_t) == WL_RPC_CLIENT_STORAGE_SIZE);
static_assert(sizeof(wl_rpc_client_slot_t) == WL_RPC_CLIENT_SLOT_STORAGE_SIZE);
static_assert(sizeof(wl_rpc_server_t) == WL_RPC_SERVER_STORAGE_SIZE);
static_assert(sizeof(wl_rpc_server_pending_slot_t) ==
              WL_RPC_SERVER_PENDING_SLOT_STORAGE_SIZE);
static_assert(sizeof(wl_rpc_server_cache_slot_t) ==
              WL_RPC_SERVER_CACHE_SLOT_STORAGE_SIZE);

int main() {
  constexpr std::uint8_t input[] = {0x00U, 0x57U, 0x4CU};
  std::uint8_t encoded[sizeof(input) + 2U]{};
  std::uint8_t decoded[sizeof(input)]{};
  std::size_t encoded_length = 0U;
  std::size_t decoded_length = 0U;

  wl_config_t config{};
  config.max_payload_len = 32U;
  config.envelope = WL_ENVELOPE_NATIVE_PACKET;
  config.integrity = WL_INTEGRITY_CRC32C;
  config.session_id = UINT64_C(0x12345678);
  config.max_retries = 1U;
  config.ack_timeout_ms = 10U;

  wl_storage_requirements_t requirements{};
  wl_ctx_t context{};
  std::uint8_t tx_payload[32]{};
  std::uint8_t tx_unit[WL_FRAME_MAX_RAW_LEN]{};
  std::uint8_t control_unit[WL_FRAME_MAX_RAW_LEN]{};
  std::uint8_t rx_fallback[WL_FRAME_MAX_RAW_LEN]{};
  wl_storage_t storage{
      tx_payload, sizeof(tx_payload), tx_unit, sizeof(tx_unit),
      control_unit, sizeof(control_unit), nullptr, 0U,
      rx_fallback, sizeof(rx_fallback)};
  wl_poll_hint_t poll_hint{};
  const bool cobs_ok =
      wl_cobs_encode(input, sizeof(input), encoded, sizeof(encoded),
                     &encoded_length) == WL_OK &&
      wl_cobs_decode(encoded, encoded_length, decoded, sizeof(decoded),
                     &decoded_length) == WL_OK &&
      decoded_length == sizeof(input);
  const bool crc_ok = wl_crc16_ccitt_false(input, sizeof(input)) != 0U &&
                      wl_crc32c(input, sizeof(input)) != 0U;
  const bool config_ok =
      wl_config_requirements(&config, &requirements) == WL_OK &&
      requirements.tx_payload_size == config.max_payload_len &&
      requirements.rx_fifo_size == 0U;
  const bool hint_ok = wl_init(&context, &config, &storage) == WL_OK &&
                       wl_poll_get_hint(&context, 0U, &poll_hint) == WL_OK &&
                       poll_hint.work_pending == 0U &&
                       poll_hint.next_deadline_ms ==
                           WL_POLL_NO_DEADLINE_MS;
  const bool version_ok = WIRELINK_PROTOCOL_VERSION == WL_FRAME_VERSION;

  return cobs_ok && crc_ok && config_ok && hint_ok && version_ok ? 0 : 1;
}
