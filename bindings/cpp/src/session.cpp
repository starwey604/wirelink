/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/cpp/session.hpp>
#include <wirelink/asio/udp_adapter.hpp>
#include <wirelink/host/executor.hpp>
#include <condition_variable>
#include <mutex>
#include <new>

namespace wirelink {

struct Session::Impl {
  // The executor binding outlives endpoint storage, including after close.
  host::Executor executor;
  EndpointFactory factory{};
  void* endpoint{};
  wl_endpoint_driver_t driver{};
  std::unique_ptr<asio::UdpAdapter> adapter;
  std::uint16_t port{};
  bool executor_bound{};
  std::mutex close_mutex;
  mutable std::mutex gate;
  std::condition_variable callers_finished;
  std::size_t active_calls{};
  bool closing{};

  ~Impl() {
    close();
    if (endpoint != nullptr) factory.destroy(endpoint);
  }

  void close() noexcept {
    // Serialize stop/join as Executor::stop itself is not a multi-caller API.
    std::lock_guard close_lock(close_mutex);
    {
      std::lock_guard lock(gate);
      closing = true;
    }
    if (executor_bound) executor.stop();
    else if (endpoint != nullptr && driver.close != nullptr) (void)driver.close(driver.context);
    {
      std::unique_lock lock(gate);
      callers_finished.wait(lock, [this] { return active_calls == 0; });
    }
    // generated close has quiesced and detached the adapter before freeing RX.
    adapter.reset();
  }
};

Session::Session(std::shared_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Session::~Session() = default;
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;

Result<Session> Session::connect(const UdpOptions& options, EndpointFactory factory) {
  if (options.peer_port == 0 || options.peer_address.empty() || options.bind_address.empty() ||
      options.peer_address.find('\0') != std::string::npos ||
      options.bind_address.find('\0') != std::string::npos ||
      factory.create == nullptr || factory.destroy == nullptr)
    return Error::local(WL_ERR_INVALID_ARG);
  try {
    auto impl = std::make_shared<Impl>();
    impl->factory = factory;
    Error error = Error::local(WL_ERR_INVALID_STATE);
    impl->endpoint = factory.create(impl->driver, error);
    if (impl->endpoint == nullptr) return error;
    if (impl->driver.endpoint == nullptr || impl->driver.step == nullptr ||
        impl->driver.close == nullptr) return Error::local(WL_ERR_INVALID_ARG);
    asio::UdpAdapterConfig config;
    config.bind_address = options.bind_address;
    config.bind_port = options.bind_port;
    std::error_code system_error;
    impl->adapter = asio::UdpAdapter::open(*impl->driver.endpoint, config, system_error);
    if (!impl->adapter) {
      error = Error::local(system_error == std::errc::invalid_argument
          ? WL_ERR_INVALID_ARG : WL_ERR_IO);
      error.system_error = system_error;
      return error;
    }
    int status = impl->adapter->set_peer(options.peer_address, options.peer_port);
    if (status != WL_OK) return Error::local(status);
    impl->port = impl->adapter->local_port();
    status = impl->executor.initialize(impl->driver);
    if (status != WL_OK) return Error::local(status);
    impl->executor_bound = true;
    status = impl->executor.start();
    if (status != WL_OK) return Error::local(status);
    return Session(std::move(impl));
  } catch (const std::bad_alloc&) {
    return Error::local(WL_ERR_NO_MEM);
  } catch (const std::system_error& failure) {
    auto error = Error::local(WL_ERR_IO);
    error.system_error = failure.code();
    return error;
  }
}

void Session::close() noexcept { if (impl_) impl_->close(); }

bool Session::is_open() const noexcept {
  if (!impl_) return false;
  std::lock_guard lock(impl_->gate);
  return !impl_->closing && impl_->executor.state() == host::Executor::State::kRunning;
}

std::uint16_t Session::local_port() const noexcept { return impl_ ? impl_->port : 0; }

wl_rpc_completion_t Session::invoke(InvokeFn function, void* call) noexcept {
  if (!impl_) return Error::local(WL_ERR_NOT_INITIALIZED).completion;
  if (function == nullptr) return Error::local(WL_ERR_INVALID_ARG).completion;
  {
    std::lock_guard lock(impl_->gate);
    if (impl_->closing) return Error::local(WL_ERR_NOT_INITIALIZED).completion;
    ++impl_->active_calls;
  }
  const auto result = function(impl_->endpoint, call);
  {
    std::lock_guard lock(impl_->gate);
    if (--impl_->active_calls == 0) impl_->callers_finished.notify_all();
  }
  return result;
}

wl_err_t Session::submit(std::shared_ptr<detail::AsyncCall> call,
    std::uint32_t timeout_ms) noexcept {
  if (!impl_) return WL_ERR_NOT_INITIALIZED;
  if (!call) return WL_ERR_INVALID_ARG;
  try {
    std::lock_guard lock(impl_->gate);
    if (impl_->closing) return WL_ERR_NOT_INITIALIZED;
    call->endpoint = impl_->endpoint;
    call->request_cancel = [owner = std::weak_ptr<Impl>(impl_), task = call.get()] {
      const auto impl = owner.lock();
      if (!impl) return false;
      std::lock_guard lock(impl->gate);
      return !impl->closing && impl->executor.cancelRpc(task);
    };
    return impl_->executor.submitRpc(std::move(call), timeout_ms);
  } catch (const std::bad_alloc&) {
    return WL_ERR_NO_MEM;
  }
}

} // namespace wirelink
