/* SPDX-License-Identifier: Apache-2.0 */
#include <calculator/client.hpp>
#include <wirelink/platform.h>
#include "calculator_endpoint.h"
#include <memory>

namespace calculator {
namespace {
void* create(wl_endpoint_driver_t& driver, wirelink::Error& error) {
  auto endpoint = std::make_unique<calculator_endpoint_t>();
  const int status = calculator_endpoint_init(endpoint.get(), wl_platform_environment());
  if (status != WL_OK) {
    error = wirelink::Error::local(status);
    return nullptr;
  }
  driver = calculator_endpoint_driver(endpoint.get());
  return endpoint.release();
}

void destroy(void* pointer) noexcept {
  auto* endpoint = static_cast<calculator_endpoint_t*>(pointer);
  (void)calculator_endpoint_close(endpoint);
  delete endpoint;
}
} // namespace

wirelink::Result<Client> Client::connect(const wirelink::UdpOptions& options) {
  auto session = wirelink::Session::connect(options, {create, destroy});
  if (!session) return session.error();
  return Client(std::move(session).value());
}

wirelink::Result<AddResponse> Client::add(const AddRequest& request,
    std::chrono::milliseconds timeout) {
  if (timeout.count() <= 0 || timeout.count() > INT32_MAX)
    return wirelink::Error::local(WL_ERR_INVALID_ARG);
  struct Call {
    add_request_value_t request{};
    add_response_value_t response{};
    std::uint32_t timeout_ms;
  } call;
  call.request.has_left = call.request.has_right = true;
  call.request.left = request.left;
  call.request.right = request.right;
  call.timeout_ms = static_cast<std::uint32_t>(timeout.count());
  const auto completion = session_.invoke([](void* endpoint, void* context) noexcept {
    auto& state = *static_cast<Call*>(context);
    return calculator_endpoint_add_sync(static_cast<calculator_endpoint_t*>(endpoint),
        &state.request, &state.response, state.timeout_ms);
  }, &call);
  if (completion.status != WL_RPC_SUCCESS)
    return wirelink::Error::from_completion(completion);
  return AddResponse{call.response.sum};
}

} // namespace calculator
