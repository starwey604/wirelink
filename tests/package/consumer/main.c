/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/wirelink.h>
#include <wirelink/frame.h>
#include <wirelink/diagnostics.h>
#include <string.h>
static int containers(void) {
  wl_latest_t latest; uint32_t latest_values[3];
  wl_latest_config_t lc = {sizeof(uint32_t), _Alignof(uint32_t), 0};
  wl_latest_storage_t ls = {latest_values, sizeof(latest_values)};
  wl_latest_write_claim_t write; wl_latest_view_t read;
  if (wl_latest_init(&latest, &lc, &ls) != WL_OK || wl_latest_write_claim(&latest, &write) != WL_OK) return 1;
  *(uint32_t *)write.value = 42;
  if (wl_latest_write_publish(&latest, &write) != WL_OK || wl_latest_read_acquire(&latest, &read) != WL_OK ||
      *(const uint32_t *)read.value != 42 || wl_latest_read_release(&latest, &read) != WL_OK) return 2;
  wl_fifo_t fifo; uint32_t fifo_values[2];
  wl_fifo_config_t fc = {sizeof(uint32_t), _Alignof(uint32_t), 2};
  wl_fifo_storage_t fs = {fifo_values, sizeof(fifo_values)};
  wl_fifo_write_claim_t claim; wl_fifo_view_t view;
  if (wl_fifo_init(&fifo, &fc, &fs) != WL_OK || wl_fifo_write_claim(&fifo, &claim) != WL_OK) return 3;
  *(uint32_t *)claim.value = 17;
  if (wl_fifo_write_publish(&fifo, &claim) != WL_OK || wl_fifo_read_acquire(&fifo, &view) != WL_OK ||
      *(const uint32_t *)view.value != 17 || wl_fifo_read_release(&fifo, &view) != WL_OK) return 4;
  return 0;
}
int main(void) {
  if (containers()) return 5;
  uint8_t bytes[64]; size_t length = 0;
  const uint64_t session = UINT64_C(0x123456789abcdef0);
  if (wl_kernel_rpc_write(bytes, sizeof(bytes), session, UINT32_MAX, -31, &length) != WL_OK) return 1;
  wl_kernel_rpc_metadata_t metadata;
  if (wl_kernel_rpc_read(bytes, length, 0, &metadata) != WL_OK || metadata.session != session ||
      metadata.operation != UINT32_MAX || metadata.status != -31 || metadata.length != 0) return 2;
  /* Historical ACK is only an independently exported framing tool. */
  wl_wire_packet_t packet = {.type = WL_PACKET_ACK, .integrity = WL_INTEGRITY_CRC16,
    .session_id = session, .sequence = 17};
  wl_frame_view_t view;
  if (wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, bytes, sizeof(bytes), &length) != WL_OK ||
      wl_frame_decode(bytes, length, WL_INTEGRITY_CRC16, &view) != WL_OK ||
      view.type != WL_PACKET_ACK || view.sequence != 17 || view.session_id != session) return 3;
  char output[128]; wl_diag_writer_t writer;
  wl_kernel_result_t rejected = {WL_KERNEL_REJECTED, -31};
  if (wl_diag_writer_init(&writer, output, sizeof(output)) != WL_OK ||
      wl_diag_format_kernel_result(&writer, &rejected) != WL_OK || !strstr(output, "-31")) return 4;
  return WL_KERNEL_CALLS != 4 || WL_KERNEL_REPLIES != 4 || WL_KERNEL_TX_COUNT != 9;
}
