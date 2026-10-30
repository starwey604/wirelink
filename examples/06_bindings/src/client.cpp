/* SPDX-License-Identifier: Apache-2.0 */
#include <calculator/client.hpp>
#include "values.hpp"
#include "endpoint.h"
#include <memory>
#include <new>
namespace calculator {
using namespace wlc_native;
namespace {
void destroy(void* pointer) noexcept {
  ::operator delete(pointer, std::align_val_t(calculator_sdk_endpoint_alignment()));
}
void* create(wl_host_endpoint_t& descriptor, wirelink::Error& error) {
  std::unique_ptr<void, decltype(&destroy)> storage(
      ::operator new(calculator_sdk_endpoint_size(), std::align_val_t(calculator_sdk_endpoint_alignment())), destroy);
  const auto status = calculator_sdk_endpoint_prepare(storage.get(), &descriptor);
  if (status != WL_OK) { error = wirelink::Error::local(status); return nullptr; }
  return storage.release();
}
}
wirelink::Result<Client> Client::connect(const wirelink::UdpOptions& options) {
  auto session = wirelink::Session::connect(options, {create, calculator_sdk_endpoint_initialize, destroy});
  if (!session) return session.error();
  return Client(std::move(session).value());
}
wirelink::Result<AddResponse> Client::add(const AddRequest& request,
    std::chrono::milliseconds timeout) {
  if (session_.on_owner_thread()) return wirelink::Error::local(WL_ERR_REENTRANT);
  auto operation = add_async(request, timeout);
  if (!operation) return operation.error();
  return operation.value().result();
}
wirelink::Result<wirelink::Operation<AddResponse>> Client::add_async(const AddRequest& request,
    std::chrono::milliseconds timeout) {
  if (timeout.count() <= 0 || timeout.count() > INT32_MAX)
    return wirelink::Error::local(WL_ERR_INVALID_ARG);
  struct wlc_call_state final : wirelink::detail::OperationState<AddResponse> {
    add_request_value_t request{};
    add_response_value_t response{};
    calculator_sdk_call_t bridge{};
    wl_err_t submit(uint32_t now, uint32_t timeout, wl_host_rpc_notify_fn notify, void* context,
        wl_kernel_call_handle_t* call) noexcept override {
      bridge.endpoint = this->endpoint;
      bridge.request = &request;
      bridge.response = &response;
      return calculator_sdk_add_submit(&bridge, now, timeout, notify, context, call);
    }
    AddResponse decode_response() override { return wlc_detail::from_c(response); }
  };
  try {
    auto call = std::make_shared<wlc_call_state>();
    if (!wlc_detail::to_c(request, call->request)) return wirelink::Error::local(WL_ERR_INVALID_ARG);
    const auto error = session_.submit(call, static_cast<uint32_t>(timeout.count()));
    if (error != WL_OK) return wirelink::Error::local(error);
    return wirelink::Operation<AddResponse>(std::move(call));
  } catch (const std::bad_alloc&) { return wirelink::Error::local(WL_ERR_NO_MEM); }
}

} // namespace calculator
