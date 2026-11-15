/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/wirelink.h>
int main(void) {
  uint8_t bytes[64]; size_t length = 0;
  const uint64_t session = UINT64_C(0x123456789abcdef0);
  if (wl_kernel_rpc_write(bytes, sizeof(bytes), session, UINT32_MAX, -31, &length) != WL_OK) return 1;
  wl_kernel_rpc_metadata_t metadata;
  if (wl_kernel_rpc_read(bytes, length, 0, &metadata) != WL_OK || metadata.session != session ||
      metadata.operation != UINT32_MAX || metadata.status != -31 || metadata.length != 0) return 2;
  return WL_KERNEL_CALLS != 4 || WL_KERNEL_REPLIES != 4 || WL_KERNEL_TX_COUNT != 9;
}
