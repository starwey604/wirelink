/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CPP_SESSION_HPP
#define WIRELINK_CPP_SESSION_HPP
#include "wirelink/cpp/result.hpp"
#include "wirelink/cpp/operation.hpp"
#include "wirelink/host/endpoint.h"
#include "wirelink/platform.h"
#include <memory>
#include <string>
namespace wirelink {
struct UdpOptions {
  std::string peer_address{"127.0.0.1"};
  uint16_t peer_port{};
  std::string bind_address{"127.0.0.1"};
  uint16_t bind_port{};
  // Read by submitters and owner; a custom/manual provider must be thread-safe.
  wl_environment_t environment{wl_platform_environment()};
};
struct EndpointFactory {
  // Prepare stable C-owned port storage/descriptor, without starting I/O.
  void *(*create)(wl_host_endpoint_t &, Error &);
  wl_err_t (*initialize)(void *, uint64_t, wl_packet_sink_fn, wl_packet_notify_fn, void *);
  // Called only after logical callbacks and real physical leases quiesce.
  void (*destroy)(void *) noexcept;
};
class Session {
public:
  static Result<Session> connect(const UdpOptions &, EndpointFactory);
  ~Session();
  Session(Session &&) noexcept;
  Session &operator=(Session &&) noexcept;
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  // External close joins the owner and physical I/O. On the owner this only
  // requests stop; an external owner must subsequently close/join. Destroying
  // or replacing the Session inside its owner callback is prohibited.
  void close() noexcept;
  // Publish external/manual-clock progress after updating the same clock.
  void notify() noexcept;
  bool is_open() const noexcept;
  bool on_owner_thread() const noexcept;
  uint16_t local_port() const noexcept;
  wl_err_t last_error() const noexcept;
  // Waits only for bounded owner-side admission, never for a remote result.
  // Success means the core has already snapshotted the request. Failure creates
  // no core call/callback. Each task object has one submission attempt.
  wl_err_t submit(std::shared_ptr<detail::AsyncCall>, uint32_t timeout_ms) noexcept;
private:
  struct Impl;
  explicit Session(std::shared_ptr<Impl>) noexcept;
  std::shared_ptr<Impl> impl_;
};
}
#endif
