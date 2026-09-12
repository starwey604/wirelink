/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_HOST_RPC_TASK_HPP
#define WIRELINK_HOST_RPC_TASK_HPP

#include <wirelink/rpc_sync.h>

namespace wirelink::host {

// Integration bridge with stable, owned request/response storage. The executor
// holds an admitted task through finish(). All three methods run on its owner;
// they must not block, throw, call stop(), or enter a language interpreter.
// submit follows wl_rpc_sync_call_t: no notification on rejected admission,
// exactly one notification otherwise, after copying callback-scoped response.
class RpcTask {
public:
  virtual ~RpcTask() = default;
  virtual wl_err_t submit(wl_time_ms_t deadline, wl_rpc_sync_notify_fn notify,
      void* context, wl_rpc_call_t* call) noexcept = 0;
  virtual wl_err_t cancel(const wl_rpc_call_t& call) noexcept = 0;
  virtual void finish(const wl_rpc_completion_t& completion) noexcept = 0;
};

} // namespace wirelink::host
#endif
