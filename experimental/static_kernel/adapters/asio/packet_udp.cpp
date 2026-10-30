/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/asio/packet_udp.hpp"
#include "wirelink/kernel_wire.h"
#include <asio.hpp>
#include <atomic>
#include <optional>
#if defined(_WIN32)
#include <mswsock.h>
#endif
namespace wirelink::asio {
class PacketUdpAdapter::Impl {
public:
  explicit Impl(wl_packet_port_t &p) : port(p), socket(io) {}
  wl_packet_port_t &port;
  ::asio::io_context io;
  ::asio::ip::udp::socket socket;
  ::asio::ip::udp::endpoint peer;
  PacketUdpConfig config;
  PacketUdpStats stats;
  wl_packet_claim_t rx_claim{};
  bool rx_claimed{};
  std::atomic<bool> wake{false};
  bool have_peer{}, started{}, started_once{}, tx_blocked{}, more_rx{};
};
PacketUdpAdapter::PacketUdpAdapter(wl_packet_port_t &port) : impl_(std::make_unique<Impl>(port)) {}
PacketUdpAdapter::~PacketUdpAdapter() { stop(); }
wl_err_t PacketUdpAdapter::start(const PacketUdpConfig &c, std::error_code &error) {
  auto &p = *impl_;
  error.clear();
  if (p.started_once)
    return WL_ERR_INVALID_STATE;
  if (!c.maximum_datagram_size || c.maximum_datagram_size > 65507 ||
      c.maximum_datagram_size >= wl_packet_rx_unit_size(&p.port) || !c.service_budget ||
      c.service_budget > 16)
    return WL_ERR_INVALID_ARG;
  auto address = ::asio::ip::make_address(c.bind_address, error);
  if (error)
    return WL_ERR_INVALID_ARG;
  p.socket.open(address.is_v6() ? ::asio::ip::udp::v6() : ::asio::ip::udp::v4(), error);
  if (error)
    return WL_ERR_IO;
#if defined(_WIN32)
  BOOL report = FALSE;
  DWORD returned = 0;
  if (::WSAIoctl(p.socket.native_handle(), SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0,
                 &returned, nullptr, nullptr) == SOCKET_ERROR) {
    error = std::error_code(::WSAGetLastError(), std::system_category());
    std::error_code ignored;
    p.socket.close(ignored);
    return WL_ERR_IO;
  }
#endif
  p.socket.bind({address, c.bind_port}, error);
  if (!error)
    p.socket.non_blocking(true, error);
  if (error) {
    std::error_code ignored;
    p.socket.close(ignored);
    return WL_ERR_IO;
  }
  p.config = c;
  p.started = p.started_once = true;
  return WL_OK;
}
wl_err_t PacketUdpAdapter::set_peer(const std::string &address, uint16_t port) {
  auto &p = *impl_;
  std::error_code error;
  if (!p.started || !port)
    return WL_ERR_INVALID_STATE;
  auto value = ::asio::ip::make_address(address, error);
  if (error)
    return WL_ERR_INVALID_ARG;
  p.peer = {value, port};
  p.have_peer = true;
  p.tx_blocked = false;
  wl_packet_activity(&p.port);
  return WL_OK;
}
wl_packet_submit_t PacketUdpAdapter::submit(void *user, const uint8_t *data, size_t length,
                                            wl_packet_token_t token) {
  (void)token;
  if (!user || !data || !length)
    return WL_PACKET_FAILED;
  auto &p = *static_cast<PacketUdpAdapter *>(user)->impl_;
  if (!p.started || length > p.config.maximum_datagram_size)
    return WL_PACKET_FAILED;
  if (!p.have_peer)
    return WL_PACKET_BUSY;
  std::error_code error;
  size_t sent = p.socket.send_to(::asio::buffer(data, length), p.peer, 0, error);
  if (error == ::asio::error::would_block || error == ::asio::error::try_again ||
      error == ::asio::error::no_buffer_space) {
    p.tx_blocked = true;
    return WL_PACKET_BUSY;
  }
  if (error || sent != length) {
    ++p.stats.errors;
    return WL_PACKET_FAILED;
  }
  ++p.stats.sent;
  p.tx_blocked = false;
  return WL_PACKET_SENT; // Whole datagram accepted; no retained user memory.
}
wl_err_t PacketUdpAdapter::service() {
  auto &p = *impl_;
  if (!p.started)
    return WL_ERR_INVALID_STATE;
  p.more_rx = false;
  for (unsigned n = 0; n < p.config.service_budget; ++n) {
    wl_err_t result = WL_OK;
    if (!p.rx_claimed) {
      result = wl_packet_rx_claim(&p.port, p.config.maximum_datagram_size + 1, &p.rx_claim);
      if (result == WL_ERR_WOULD_BLOCK)
        return WL_OK;
      if (result != WL_OK)
        return result;
      p.rx_claimed = true;
    }
    auto &claim = p.rx_claim;
    ::asio::ip::udp::endpoint source;
    std::error_code error;
    size_t received =
        p.socket.receive_from(::asio::buffer(claim.span.data, claim.span.length), source, 0, error);
    if (error == ::asio::error::would_block || error == ::asio::error::try_again) {
      return WL_OK; // Keep this idle RX reservation; no self-generated wake loop.
    }
    if (error == ::asio::error::message_size ||
        (!error && (!received || received > p.config.maximum_datagram_size)) ||
        (!error && p.have_peer && source != p.peer)) {
      (void)wl_packet_rx_abort(&p.port, &claim);
      p.rx_claimed = false;
      ++p.stats.rejected;
      continue;
    }
    if (error) {
      (void)wl_packet_rx_abort(&p.port, &claim);
      p.rx_claimed = false;
      ++p.stats.errors;
      return WL_ERR_IO;
    }
    if (p.config.decode && p.config.decode(claim.span.data, received, &received) != WL_OK) {
      (void)wl_packet_rx_abort(&p.port, &claim);
      p.rx_claimed = false;
      ++p.stats.rejected;
      continue;
    }
    if (!p.have_peer) {
      if (!p.config.learn_peer) {
        (void)wl_packet_rx_abort(&p.port, &claim);
        p.rx_claimed = false;
        ++p.stats.rejected;
        continue;
      }
      wl_kernel_frame_t frame{};
      if (wl_kernel_native_none_read(claim.span.data, received, &frame) != WL_OK) {
        (void)wl_packet_rx_abort(&p.port, &claim);
        p.rx_claimed = false;
        ++p.stats.rejected;
        continue;
      }
      p.peer = source;
      p.have_peer = true;
      wl_packet_activity(&p.port);
    }
    result = wl_packet_rx_commit(&p.port, &claim, received);
    if (result != WL_OK) {
      (void)wl_packet_rx_abort(&p.port, &claim);
      p.rx_claimed = false;
      return result;
    }
    p.rx_claimed = false;
    ++p.stats.received;
  }
  p.more_rx = true;
  return WL_OK;
}
bool PacketUdpAdapter::ready() const {
  const auto &p = *impl_;
  if (!p.started)
    return false;
  std::error_code error;
  return (p.rx_claimed || wl_packet_rx_available(&p.port)) &&
         (p.more_rx || p.socket.available(error) != 0);
}
void PacketUdpAdapter::notify(void *user) {
  if (!user)
    return;
  auto &p = *static_cast<PacketUdpAdapter *>(user)->impl_;
  p.wake.store(true, std::memory_order_release);
  p.io.stop();
}
wl_err_t PacketUdpAdapter::wait_for_activity(std::chrono::milliseconds maximum) {
  auto &p = *impl_;
  if (!p.started)
    return WL_ERR_INVALID_STATE;
  if (maximum.count() < 0)
    return WL_ERR_INVALID_ARG;
  if (ready())
    return WL_OK;
  unsigned pending = 0;
  bool ready = false, failed = false, expired = false;
  std::optional<::asio::steady_timer> timer(std::in_place, p.io);
  auto completion = [&](const std::error_code &error) {
    --pending;
    if (!error)
      ready = true;
    else if (error != ::asio::error::operation_aborted)
      failed = true;
  };
  auto drain = [&] {
    std::error_code error;
    p.socket.cancel(error);
    timer.reset();
    while (pending) {
      p.io.restart();
      p.io.poll();
    }
    return error;
  };
  try {
    p.io.restart();
    if (maximum == std::chrono::milliseconds(UINT32_MAX))
      timer->expires_at(::asio::steady_timer::time_point::max());
    else
      timer->expires_after(maximum);
    timer->async_wait([&](const std::error_code &error) {
      --pending;
      if (!error)
        expired = true;
      else if (error != ::asio::error::operation_aborted)
        failed = true;
    });
    ++pending;
    if (p.rx_claimed || wl_packet_rx_available(&p.port)) {
      p.socket.async_wait(::asio::ip::udp::socket::wait_read, completion);
      ++pending;
    }
    if (p.tx_blocked) {
      p.socket.async_wait(::asio::ip::udp::socket::wait_write, completion);
      ++pending;
    }
    if (p.wake.exchange(false, std::memory_order_acquire))
      ready = true;
    while (!ready && !failed && !expired && !p.wake.load(std::memory_order_acquire))
      p.io.run_one();
  } catch (...) {
    (void)drain();
    throw;
  }
  const auto error = drain();
  if (p.wake.exchange(false, std::memory_order_acquire))
    ready = true;
  if (failed || error)
    return WL_ERR_IO;
  if (ready) {
    p.more_rx = true;
    if (p.tx_blocked) {
      p.tx_blocked = false;
      wl_packet_activity(&p.port);
    }
  }
  return ready ? WL_OK : WL_ERR_NO_DATA;
}
void PacketUdpAdapter::stop() {
  auto &p = *impl_;
  if (!p.started)
    return;
  p.started = false;
  if (p.rx_claimed) {
    (void)wl_packet_rx_abort(&p.port, &p.rx_claim);
    p.rx_claimed = false;
  }
  wl_packet_stop(&p.port);
  notify(this);
  std::error_code error;
  p.socket.close(error);
}
uint16_t PacketUdpAdapter::local_port() const {
  std::error_code error;
  auto endpoint = impl_->socket.local_endpoint(error);
  return error ? 0 : endpoint.port();
}
PacketUdpStats PacketUdpAdapter::stats() const { return impl_->stats; }
} // namespace wirelink::asio
