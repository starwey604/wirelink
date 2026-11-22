/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/asio/packet_udp.hpp"
#include "wirelink/wire.h"
#include <asio.hpp>
#include <algorithm>
#include <atomic>
#include <optional>
#include <utility>
#if defined(_WIN32)
#include <mswsock.h>
#endif
struct wl_asio_udp {
  explicit wl_asio_udp(wl_packet_port_t &port_ref) : port(port_ref), socket(io) {}

  wl_err_t start(const wl_asio_udp_config_t &config, std::error_code &error);
  wl_err_t set_peer(const char *address, uint16_t port_number);
  void stop();
  wl_err_t service();
  bool ready() const;
  wl_err_t wait_for_activity(std::chrono::milliseconds);
  uint16_t local_port() const;
  wl_packet_submit_t submit(const uint8_t *data, size_t length);
  void notify();

  wl_packet_port_t &port;
  wl_asio_udp_stats_t stats{};

private:
  void set_peer(const ::asio::ip::udp::endpoint &endpoint);

  ::asio::io_context io;
  ::asio::ip::udp::socket socket;
  ::asio::ip::udp::endpoint peer;
  size_t maximum_datagram_size{}, maximum_tx_datagram_size{};
  unsigned service_budget{};
  bool learn_peer{};
  wl_err_t (*decode)(uint8_t *, size_t, size_t *){};
  wl_packet_claim_t rx_claim{};
  bool rx_claimed{};
  std::atomic<bool> wake{false};
  bool have_peer{}, started{}, started_once{}, tx_blocked{}, more_rx{};
};

wl_err_t wl_asio_udp::start(const wl_asio_udp_config_t &config, std::error_code &error) {
  error.clear();
  if (started_once) return WL_ERR_INVALID_STATE;
  const size_t unit = wl_packet_rx_unit_size(&port);
  if (!config.maximum_datagram_size && unit < 5) return WL_ERR_INVALID_ARG;
  const size_t rx_limit = config.maximum_datagram_size ? config.maximum_datagram_size : unit - 1;
  if (rx_limit > 65507 || rx_limit >= unit || config.maximum_tx_datagram_size > 65507 ||
      !config.service_budget || config.service_budget > 16 ||
      (config.peer_address == nullptr) != (config.peer_port == 0))
    return WL_ERR_INVALID_ARG;

  const auto local = ::asio::ip::make_address(config.bind_address ? config.bind_address : "0.0.0.0", error);
  if (error) return WL_ERR_INVALID_ARG;

  ::asio::ip::udp::endpoint initial_peer;
  if (config.peer_address) {
    const auto address = ::asio::ip::make_address(config.peer_address, error);
    if (error || address.is_v6() != local.is_v6()) return WL_ERR_INVALID_ARG;
    initial_peer = {address, config.peer_port};
  }

  ::asio::ip::udp::socket pending_socket(io);
  pending_socket.open(local.is_v6() ? ::asio::ip::udp::v6() : ::asio::ip::udp::v4(), error);
  if (error) return WL_ERR_IO;
#if defined(_WIN32)
  BOOL report = FALSE;
  DWORD returned = 0;
  if (::WSAIoctl(pending_socket.native_handle(), SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0,
                 &returned, nullptr, nullptr) == SOCKET_ERROR) {
    error = std::error_code(::WSAGetLastError(), std::system_category());
    return WL_ERR_IO;
  }
#endif
  pending_socket.bind({local, config.bind_port}, error);
  if (!error) pending_socket.non_blocking(true, error);
  if (error) return WL_ERR_IO;

  socket = std::move(pending_socket);
  maximum_datagram_size = rx_limit;
  maximum_tx_datagram_size = config.maximum_tx_datagram_size ? config.maximum_tx_datagram_size : rx_limit;
  service_budget = config.service_budget;
  learn_peer = config.learn_peer;
  decode = config.decode;
  started = started_once = true;
  if (config.peer_address) set_peer(initial_peer);
  return WL_OK;
}

void wl_asio_udp::set_peer(const ::asio::ip::udp::endpoint &endpoint) {
  peer = endpoint;
  have_peer = true;
  tx_blocked = false;
  wl_packet_activity(&port);
}

wl_err_t wl_asio_udp::set_peer(const char *address, uint16_t port_number) {
  if (!started || !port_number) return WL_ERR_INVALID_STATE;
  std::error_code error;
  const auto value = ::asio::ip::make_address(address, error);
  if (error) return WL_ERR_INVALID_ARG;
  set_peer({value, port_number});
  return WL_OK;
}

wl_packet_submit_t wl_asio_udp::submit(const uint8_t *data, size_t length) {
  if (!data || !length || !started || length > maximum_tx_datagram_size)
    return WL_PACKET_FAILED;
  if (!have_peer)
    return WL_PACKET_BUSY;
  std::error_code error;
  const size_t sent = socket.send_to(::asio::buffer(data, length), peer, 0, error);
  if (error == ::asio::error::would_block || error == ::asio::error::try_again ||
      error == ::asio::error::no_buffer_space) {
    tx_blocked = true;
    return WL_PACKET_BUSY;
  }
  if (error || sent != length) {
    ++stats.errors;
    return WL_PACKET_FAILED;
  }
  ++stats.sent;
  tx_blocked = false;
  return WL_PACKET_SENT; // Whole datagram accepted; no retained user memory.
}

wl_err_t wl_asio_udp::service() {
  if (!started)
    return WL_ERR_INVALID_STATE;
  more_rx = false;
  for (unsigned n = 0; n < service_budget; ++n) {
    wl_err_t result = WL_OK;
    if (!rx_claimed) {
      result = wl_packet_rx_claim(&port, maximum_datagram_size + 1, &rx_claim);
      if (result == WL_ERR_WOULD_BLOCK)
        return WL_OK;
      if (result != WL_OK)
        return result;
      rx_claimed = true;
    }
    auto &claim = rx_claim;
    ::asio::ip::udp::endpoint source;
    std::error_code error;
    size_t received =
        socket.receive_from(::asio::buffer(claim.span.data, claim.span.length), source, 0, error);
    if (error == ::asio::error::would_block || error == ::asio::error::try_again) {
      return WL_OK; // Keep this idle RX reservation; no self-generated wake loop.
    }
    if (error == ::asio::error::message_size ||
        (!error && (!received || received > maximum_datagram_size)) ||
        (!error && have_peer && source != peer)) {
      (void)wl_packet_rx_abort(&port, &claim);
      rx_claimed = false;
      ++stats.rejected;
      continue;
    }
    if (error) {
      (void)wl_packet_rx_abort(&port, &claim);
      rx_claimed = false;
      ++stats.errors;
      return WL_ERR_IO;
    }
    if (decode && decode(claim.span.data, received, &received) != WL_OK) {
      (void)wl_packet_rx_abort(&port, &claim);
      rx_claimed = false;
      ++stats.rejected;
      continue;
    }
    if (!have_peer) {
      if (!learn_peer) {
        (void)wl_packet_rx_abort(&port, &claim);
        rx_claimed = false;
        ++stats.rejected;
        continue;
      }
      wl_wire_frame_t frame{};
      if (wl_wire_native_none_read(claim.span.data, received, &frame) != WL_OK) {
        (void)wl_packet_rx_abort(&port, &claim);
        rx_claimed = false;
        ++stats.rejected;
        continue;
      }
      set_peer(source);
    }
    result = wl_packet_rx_commit(&port, &claim, received);
    if (result != WL_OK) {
      (void)wl_packet_rx_abort(&port, &claim);
      rx_claimed = false;
      return result;
    }
    rx_claimed = false;
    ++stats.received;
  }
  more_rx = true;
  return WL_OK;
}
bool wl_asio_udp::ready() const {
  if (!started)
    return false;
  std::error_code error;
  return (rx_claimed || wl_packet_rx_available(&port)) &&
         (more_rx || socket.available(error) != 0);
}
void wl_asio_udp::notify() {
  wake.store(true, std::memory_order_release);
  io.stop();
}
wl_err_t wl_asio_udp::wait_for_activity(std::chrono::milliseconds maximum) {
  if (!started)
    return WL_ERR_INVALID_STATE;
  if (maximum.count() < 0)
    return WL_ERR_INVALID_ARG;
  if (ready())
    return WL_OK;
  unsigned pending = 0;
  bool ready = false, failed = false, expired = false;
  std::optional<::asio::steady_timer> timer(std::in_place, io);
  auto completion = [&](const std::error_code &error) {
    --pending;
    if (!error)
      ready = true;
    else if (error != ::asio::error::operation_aborted)
      failed = true;
  };
  auto drain = [&] {
    std::error_code error;
    socket.cancel(error);
    timer.reset();
    while (pending) {
      io.restart();
      io.poll();
    }
    return error;
  };
  try {
    io.restart();
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
    if (rx_claimed || wl_packet_rx_available(&port)) {
      socket.async_wait(::asio::ip::udp::socket::wait_read, completion);
      ++pending;
    }
    if (tx_blocked) {
      socket.async_wait(::asio::ip::udp::socket::wait_write, completion);
      ++pending;
    }
    if (wake.exchange(false, std::memory_order_acquire))
      ready = true;
    while (!ready && !failed && !expired && !wake.load(std::memory_order_acquire))
      io.run_one();
  } catch (...) {
    (void)drain();
    throw;
  }
  const auto error = drain();
  if (wake.exchange(false, std::memory_order_acquire))
    ready = true;
  if (failed || error)
    return WL_ERR_IO;
  if (ready) {
    more_rx = true;
    if (tx_blocked) {
      tx_blocked = false;
      wl_packet_activity(&port);
    }
  }
  return ready ? WL_OK : WL_ERR_NO_DATA;
}
void wl_asio_udp::stop() {
  if (!started)
    return;
  started = false;
  if (rx_claimed) {
    (void)wl_packet_rx_abort(&port, &rx_claim);
    rx_claimed = false;
  }
  wl_packet_stop(&port);
  notify();
  std::error_code error;
  socket.close(error);
}
uint16_t wl_asio_udp::local_port() const {
  std::error_code error;
  const auto endpoint = socket.local_endpoint(error);
  return error ? 0 : endpoint.port();
}

namespace wirelink::asio {

PacketUdpAdapter::PacketUdpAdapter(wl_packet_port_t &port) : impl_(std::make_unique<Impl>(port)) {}
PacketUdpAdapter::~PacketUdpAdapter() { stop(); }

wl_err_t PacketUdpAdapter::start(const PacketUdpConfig &config, std::error_code &error) {
  return impl_->start({config.bind_address.c_str(), config.bind_port, nullptr, 0,
                       config.maximum_datagram_size, config.maximum_tx_datagram_size,
                       config.service_budget, config.learn_peer, config.decode}, error);
}

wl_err_t PacketUdpAdapter::set_peer(const std::string &address, uint16_t port) {
  return impl_->set_peer(address.c_str(), port);
}

void PacketUdpAdapter::stop() { impl_->stop(); }
wl_err_t PacketUdpAdapter::service() { return impl_->service(); }
bool PacketUdpAdapter::ready() const { return impl_->ready(); }
wl_err_t PacketUdpAdapter::wait_for_activity(std::chrono::milliseconds maximum) {
  return impl_->wait_for_activity(maximum);
}
uint16_t PacketUdpAdapter::local_port() const { return impl_->local_port(); }
PacketUdpStats PacketUdpAdapter::stats() const {
  const auto &stats = impl_->stats;
  return {stats.received, stats.sent, stats.rejected, stats.errors};
}

wl_packet_submit_t PacketUdpAdapter::submit(void *user, const uint8_t *data, size_t length,
                                          wl_packet_token_t) {
  return user ? static_cast<PacketUdpAdapter *>(user)->impl_->submit(data, length) : WL_PACKET_FAILED;
}

void PacketUdpAdapter::notify(void *user) {
  if (user) static_cast<PacketUdpAdapter *>(user)->impl_->notify();
}

} // namespace wirelink::asio

extern "C" {

wl_asio_udp_t *wl_asio_udp_create(wl_packet_port_t *port) {
  try { return port ? new wl_asio_udp(*port) : nullptr; }
  catch (...) { return nullptr; }
}

void wl_asio_udp_destroy(wl_asio_udp_t *udp) noexcept {
  wl_asio_udp_stop(udp);
  delete udp;
}

wl_packet_io_t wl_asio_udp_io(wl_asio_udp_t *udp) {
  return udp ? wl_packet_io_t{wl_asio_udp_submit, wl_asio_udp_notify, udp, nullptr, 0, 0}
             : wl_packet_io_t{};
}

wl_err_t wl_asio_udp_start(wl_asio_udp_t *udp, const wl_asio_udp_config_t *config) {
  if (!udp || !config) return WL_ERR_INVALID_ARG;
  try {
    auto settings = *config;
    if (!settings.service_budget) settings.service_budget = 8;
    std::error_code error;
    return udp->start(settings, error);
  } catch (...) { return WL_ERR_IO; }
}

wl_err_t wl_asio_udp_set_peer(wl_asio_udp_t *udp, const char *address, uint16_t port) {
  if (!udp || !address || !port) return WL_ERR_INVALID_ARG;
  try { return udp->set_peer(address, port); }
  catch (...) { return WL_ERR_IO; }
}

void wl_asio_udp_stop(wl_asio_udp_t *udp) noexcept {
  if (!udp) return;
  try { udp->stop(); }
  catch (...) {}
}

wl_err_t wl_asio_udp_service(wl_asio_udp_t *udp) {
  if (!udp) return WL_ERR_INVALID_ARG;
  try { return udp->service(); }
  catch (...) { return WL_ERR_IO; }
}

bool wl_asio_udp_ready(const wl_asio_udp_t *udp) {
  try { return udp && udp->ready(); }
  catch (...) { return false; }
}

wl_err_t wl_asio_udp_wait(wl_asio_udp_t *udp, uint32_t maximum_ms) {
  if (!udp) return WL_ERR_INVALID_ARG;
  try { return udp->wait_for_activity(std::chrono::milliseconds(maximum_ms)); }
  catch (...) { return WL_ERR_IO; }
}

wl_err_t wl_asio_udp_step_endpoint(wl_asio_udp_t *udp, wl_endpoint_t *core, uint32_t now_ms) {
  if (!udp || !core || core->port != &udp->port) return WL_ERR_INVALID_ARG;
  try {
    const auto status = wl_asio_udp_service(udp);
    const auto step = wl_endpoint_step(core, now_ms, WL_ENDPOINT_BUDGET);
    return status == WL_OK ? step : status;
  } catch (...) { return WL_ERR_IO; }
}

wl_err_t wl_asio_udp_wait_endpoint(wl_asio_udp_t *udp, wl_endpoint_t *core,
                                  uint32_t now_ms, uint32_t maximum_ms) {
  if (!udp || !core || core->port != &udp->port) return WL_ERR_INVALID_ARG;
  try {
    int has_deadline = 0;
    uint32_t deadline = 0;
    if (wl_endpoint_ready(core, now_ms, &has_deadline, &deadline)) return WL_OK;
    const uint32_t limit = has_deadline ? std::min(maximum_ms, deadline - now_ms) : maximum_ms;
    const auto status = wl_asio_udp_wait(udp, limit);
    return status == WL_ERR_NO_DATA ? WL_OK : status;
  } catch (...) { return WL_ERR_IO; }
}

wl_err_t wl_asio_udp_close_endpoint(wl_asio_udp_t *udp, wl_endpoint_t *core, uint32_t now_ms) {
  if (!udp || !core || core->port != &udp->port) return WL_ERR_INVALID_ARG;
  try {
    const auto closed = wl_endpoint_close(core);
    if (closed != WL_OK) return closed;
    udp->stop();
    const auto step = wl_endpoint_step(core, now_ms, WL_ENDPOINT_BUDGET);
    if (step != WL_OK) return step;
    return wl_endpoint_quiescent(core) ? WL_OK : WL_ERR_BUSY;
  } catch (...) { return WL_ERR_IO; }
}

uint16_t wl_asio_udp_local_port(const wl_asio_udp_t *udp) {
  try { return udp ? udp->local_port() : 0; }
  catch (...) { return 0; }
}

wl_asio_udp_stats_t wl_asio_udp_stats(const wl_asio_udp_t *udp) {
  return udp ? udp->stats : wl_asio_udp_stats_t{};
}

wl_packet_submit_t wl_asio_udp_submit(void *user, const uint8_t *data, size_t length,
                                    wl_packet_token_t) {
  try { return user ? static_cast<wl_asio_udp *>(user)->submit(data, length) : WL_PACKET_FAILED; }
  catch (...) { return WL_PACKET_FAILED; }
}

void wl_asio_udp_notify(void *user) {
  if (!user) return;
  try { static_cast<wl_asio_udp *>(user)->notify(); }
  catch (...) {}
}

} // extern "C"
