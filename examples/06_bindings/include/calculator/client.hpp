/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CALCULATOR_CLIENT_HPP
#define CALCULATOR_CLIENT_HPP

#include <wirelink/cpp/session.hpp>
#include <chrono>
#include <cstdint>
#include <utility>

namespace calculator {

// Handwritten reference façade for the first binding iteration.
// No generated C layout or borrowed storage crosses this public interface.
struct AddRequest { std::int32_t left; std::int32_t right; };
struct AddResponse { std::int32_t sum; };

class Client {
public:
  static wirelink::Result<Client> connect(const wirelink::UdpOptions& options);
  Client(Client&&) noexcept = default;
  Client& operator=(Client&&) noexcept = default;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  wirelink::Result<AddResponse> add(const AddRequest& request,
      std::chrono::milliseconds timeout = std::chrono::seconds(1));
  void close() noexcept { session_.close(); }
  bool is_open() const noexcept { return session_.is_open(); }
  std::uint16_t local_port() const noexcept { return session_.local_port(); }

private:
  explicit Client(wirelink::Session session) : session_(std::move(session)) {}
  wirelink::Session session_;
};

} // namespace calculator
#endif
