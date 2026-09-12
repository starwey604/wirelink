/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CPP_SESSION_HPP
#define WIRELINK_CPP_SESSION_HPP

#include <wirelink/cpp/result.hpp>
#include <wirelink/cpp/operation.hpp>
#include <wirelink/endpoint.h>
#include <cstdint>
#include <memory>
#include <string>

namespace wirelink {

struct UdpOptions {
  std::string peer_address{"127.0.0.1"};
  std::uint16_t peer_port{};
  std::string bind_address{"127.0.0.1"};
  std::uint16_t bind_port{};
};

// Generator/integration bridge. create initializes stable native storage and
// supplies its generated driver. A failed create returns nullptr and frees
// its own partial allocation. destroy closes and frees the complete endpoint.
// Neither callback may retain the driver reference or throw from destroy.
struct EndpointFactory {
  void* (*create)(wl_endpoint_driver_t& driver, Error& error);
  void (*destroy)(void* endpoint) noexcept;
};

// Owning, single-owner host connection used behind schema-specific Clients.
// invoke and close may run concurrently; close rejects new calls and waits
// for admitted calls. Move/destruction require exclusive access to this handle.
// Moving the handle never relocates initialized endpoint/executor storage.
class Session {
public:
  static Result<Session> connect(const UdpOptions& options, EndpointFactory factory);
  ~Session();
  Session(Session&&) noexcept;
  Session& operator=(Session&&) noexcept;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  void close() noexcept;
  bool is_open() const noexcept;
  std::uint16_t local_port() const noexcept;

  // The bridge must call the generated *_sync API, never poll/step or close.
  // Both pointers remain valid until invoke returns; no asynchronous escape.
  using InvokeFn = wl_rpc_completion_t (*)(void* endpoint, void* call) noexcept;
  wl_rpc_completion_t invoke(InvokeFn function, void* call) noexcept;

  // Generator bridge. Success retains the owned call through completion;
  // rejection leaves it unsubmitted. Each call object may be submitted once.
  wl_err_t submit(std::shared_ptr<detail::AsyncCall> call, std::uint32_t timeout_ms) noexcept;

private:
  struct Impl;
  explicit Session(std::shared_ptr<Impl> impl) noexcept;
  std::shared_ptr<Impl> impl_;
};

} // namespace wirelink
#endif
