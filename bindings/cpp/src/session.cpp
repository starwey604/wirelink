/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/cpp/session.hpp"
#include "wirelink/asio/packet_udp.hpp"
#include "wirelink/host/executor.hpp"
#include <condition_variable>
#include <mutex>
#include <new>
namespace wirelink {
struct Session::Impl {
  host::Executor executor;
  EndpointFactory factory{};
  wl_host_endpoint_t driver{};
  void *endpoint{};
  std::unique_ptr<asio::PacketUdpAdapter> adapter;
  wl_environment_t environment{};
  uint16_t port{};
  bool initialized{}, started{};
  std::mutex close_gate;
  mutable std::mutex gate;
  std::condition_variable callers_finished;
  size_t callers{};
  bool closing{};
  ~Impl() { close(); if (endpoint) factory.destroy(endpoint); }
  bool enter() {
    std::lock_guard lock(gate);
    if (closing) return false;
    ++callers; return true;
  }
  void leave() {
    std::lock_guard lock(gate);
    if (--callers == 0) callers_finished.notify_all();
  }
  void close() noexcept {
    if (executor.on_owner_thread()) {
      { std::lock_guard lock(gate); closing = true; }
      executor.request_stop(); return;
    }
    std::lock_guard close_lock(close_gate);
    {
      std::unique_lock lock(gate);
      closing = true;
      callers_finished.wait(lock, [this] { return callers == 0; });
    }
    if (started) (void)executor.stop();
    else if (initialized) {
      (void)wl_kernel_close(driver.core);
      if (adapter) adapter->stop();
      (void)wl_kernel_step(driver.core, environment.clock.now_ms(environment.clock.user_data), 16);
    }
    adapter.reset();
  }
  wl_err_t admit(const std::shared_ptr<detail::AsyncCall> &call, uint32_t began,
                  uint32_t timeout, uint32_t now) {
    uint32_t elapsed = now - began;
    if (elapsed >= timeout) return WL_ERR_TIMEOUT;
    call->endpoint = driver.context;
    call->keepalive = call;
    wl_err_t status = call->submit(now, timeout - elapsed,
        [](void *context, wl_kernel_result_t result) {
          auto *task = static_cast<detail::AsyncCall *>(context);
          auto lifetime = std::move(task->keepalive);
          task->call = {};
          task->finish(result);
        }, call.get(), &call->call);
    if (status != WL_OK) call->keepalive.reset();
    return status;
  }
};
Session::Session(std::shared_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Session::~Session() {
  if (on_owner_thread()) std::terminate();
  close();
}
Session::Session(Session &&) noexcept = default;
Session &Session::operator=(Session &&other) noexcept {
  if (this != &other) {
    if (on_owner_thread()) std::terminate();
    close(); impl_ = std::move(other.impl_);
  }
  return *this;
}
Result<Session> Session::connect(const UdpOptions &options, EndpointFactory factory) {
  if (!options.peer_port || options.peer_address.empty() || options.bind_address.empty() ||
      options.peer_address.find('\0') != std::string::npos ||
      options.bind_address.find('\0') != std::string::npos ||
      !options.environment.clock.now_ms || !options.environment.session.next ||
      !factory.create || !factory.initialize || !factory.destroy)
    return Error::local(WL_ERR_INVALID_ARG);
  try {
    auto p = std::make_shared<Impl>();
    p->factory = factory; p->environment = options.environment;
    auto error = Error::local(WL_ERR_INVALID_STATE);
    p->endpoint = factory.create(p->driver, error);
    if (!p->endpoint) return error;
    if (!p->driver.context || !p->driver.core || !p->driver.port ||
        p->driver.maximum_rx_frame < 4 || p->driver.maximum_rx_frame > 2052)
      return Error::local(WL_ERR_INVALID_ARG);
    p->adapter = std::make_unique<asio::PacketUdpAdapter>(*p->driver.port);
    uint64_t instance = 0;
    auto status = wl_session_next(options.environment.session, 0, &instance);
    if (status != WL_OK) return Error::local(status);
    status = factory.initialize(p->endpoint, instance, asio::PacketUdpAdapter::submit,
                               asio::PacketUdpAdapter::notify, p->adapter.get());
    if (status != WL_OK) return Error::local(status);
    p->initialized = true;
    asio::PacketUdpConfig config;
    config.bind_address = options.bind_address; config.bind_port = options.bind_port;
    config.maximum_datagram_size = p->driver.maximum_rx_frame;
    std::error_code os_error;
    status = p->adapter->start(config, os_error);
    if (status != WL_OK) { error = Error::local(status); error.system_error = os_error; return error; }
    status = p->adapter->set_peer(options.peer_address, options.peer_port);
    if (status != WL_OK) return Error::local(status);
    p->port = p->adapter->local_port();
    host::ExecutorIo io;
    io.user = p.get();
    io.service = [](void *x, uint32_t) { return static_cast<Impl *>(x)->adapter->service(); };
    io.readiness = [](void *x, uint32_t) {
      return wl_io_readiness_at(static_cast<Impl *>(x)->adapter->ready() ? 0U : UINT32_MAX);
    };
    io.wait = [](void *x, uint32_t limit) {
      return static_cast<Impl *>(x)->adapter->wait_for_activity(std::chrono::milliseconds(limit));
    };
    io.notify = [](void *x) {
      auto *self = static_cast<Impl *>(x);
      if (self->adapter) asio::PacketUdpAdapter::notify(self->adapter.get());
    };
    io.stop = [](void *x) { static_cast<Impl *>(x)->adapter->stop(); };
    status = p->executor.initialize(*p->driver.core, io, options.environment.clock);
    if (status == WL_OK) status = p->executor.start();
    if (status != WL_OK) return Error::local(status);
    p->started = true;
    return Session(std::move(p));
  } catch (const std::bad_alloc &) { return Error::local(WL_ERR_NO_MEM); }
    catch (const std::system_error &failure) {
      auto error = Error::local(WL_ERR_IO); error.system_error = failure.code(); return error;
    }
}
void Session::close() noexcept { if (impl_) impl_->close(); }
void Session::notify() noexcept {
  auto p = impl_;
  if (!p || !p->enter()) return;
  p->executor.notify();
  p->leave();
}
bool Session::is_open() const noexcept {
  if (!impl_) return false;
  std::lock_guard lock(impl_->gate);
  return !impl_->closing && impl_->executor.state() == host::Executor::State::running;
}
bool Session::on_owner_thread() const noexcept { return impl_ && impl_->executor.on_owner_thread(); }
uint16_t Session::local_port() const noexcept { return impl_ ? impl_->port : 0; }
wl_err_t Session::last_error() const noexcept {
  return impl_ ? impl_->executor.last_error() : WL_ERR_NOT_INITIALIZED;
}
wl_err_t Session::submit(std::shared_ptr<detail::AsyncCall> call, uint32_t timeout) noexcept {
  auto p = impl_;
  if (!p) return WL_ERR_NOT_INITIALIZED;
  if (!call || timeout == 0 || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  if (!p->enter()) return WL_ERR_NOT_INITIALIZED;
  struct Guard { Impl *p; ~Guard() { p->leave(); } } guard{p.get()};
  if (p->executor.state() != host::Executor::State::running) return WL_ERR_NOT_INITIALIZED;
  bool unused = false;
  if (!call->submitted.compare_exchange_strong(unused, true)) return WL_ERR_INVALID_STATE;
  const auto began = p->environment.clock.now_ms(p->environment.clock.user_data);
  try {
    std::weak_ptr<Impl> owner = p;
    std::weak_ptr<detail::AsyncCall> operation = call;
    call->request_cancel = [owner, operation] {
      auto impl = owner.lock(); auto task = operation.lock();
      if (!impl || !task || !impl->enter()) return false;
      struct Leave { Impl *p; ~Leave() { p->leave(); } } leave{impl.get()};
      try {
        return impl->executor.post([self = impl.get(), task](uint32_t) {
          if (task->call.instance) (void)wl_kernel_cancel(self->driver.core, task->call);
        }) == WL_OK;
      } catch (const std::bad_alloc &) { return false; }
    };
    if (p->executor.on_owner_thread()) return p->admit(call, began, timeout, began);
    struct Admission {
      std::mutex mutex; std::condition_variable changed;
      bool done{}; wl_err_t result{WL_ERR_INVALID_STATE};
      void complete(wl_err_t value) {
        std::lock_guard lock(mutex); result = value; done = true; changed.notify_one();
      }
    } admission;
    const auto status = p->executor.post(
        [self = p.get(), call, began, timeout, &admission](uint32_t now) {
          admission.complete(self->admit(call, began, timeout, now));
        }, [&admission](wl_err_t error) { admission.complete(error); });
    if (status != WL_OK) return status;
    std::unique_lock lock(admission.mutex);
    admission.changed.wait(lock, [&] { return admission.done; });
    return admission.result;
  } catch (const std::bad_alloc &) { return WL_ERR_NO_MEM; }
}
} // namespace wirelink
