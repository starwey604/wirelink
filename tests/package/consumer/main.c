/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/wirelink.h>
int main(void) {
  uint8_t bytes[64]; size_t length = 0;
  const uint32_t session = UINT32_C(0x12345678);
  if (wl_wire_rpc_write(bytes, sizeof(bytes), session, UINT32_MAX, -31, &length) != WL_OK) return 1;
  wl_kernel_rpc_metadata_t metadata;
  if (wl_wire_rpc_read(WL_WIRE_RPC_ERR, bytes, length, &metadata) != WL_OK || metadata.session != session ||
      metadata.operation != UINT32_MAX || metadata.status != -31 || metadata.length != 0) return 2;
  return WL_KERNEL_CALLS != 4 || WL_KERNEL_REPLIES != 3 || WL_KERNEL_TX_COUNT != 8;
}
