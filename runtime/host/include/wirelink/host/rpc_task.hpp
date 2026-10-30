/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_HOST_RPC_TASK_HPP
#define WIRELINK_HOST_RPC_TASK_HPP
#include "wirelink/host/endpoint.h"
namespace wirelink::host {
// Owned language result/request bridge only. All methods run on the owner.
// The kernel owns admission, response matching, timeout and the four call slots.
class RpcTask {
public:
  virtual ~RpcTask() = default;
  virtual wl_err_t submit(uint32_t now, uint32_t timeout, wl_host_rpc_notify_fn,
                          void *, wl_kernel_call_handle_t *) noexcept = 0;
  virtual void finish(wl_kernel_result_t) noexcept = 0;
};
}
#endif
