/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/udp.h"
#include "wirelink/wire.h"
#include <asio.hpp>
#include <cerrno>
#include <chrono>
#include <climits>
#include <memory>
#include <atomic>
#include <utility>
#if defined(_WIN32)
#include <mswsock.h>
#else
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#endif

namespace {
// The owner waits with one poll() over the data socket and a self-connected
// loopback wake socket instead of running an Asio reactor per wait: no timer,
// no epoll registration churn, one syscall per idle wait.
#if defined(_WIN32)
using poll_fd = WSAPOLLFD;
int poll_sockets(poll_fd *fds, unsigned count, int timeout) { return ::WSAPoll(fds, count, timeout); }
bool poll_interrupted() { return false; }
#else
using poll_fd = pollfd;
int poll_sockets(poll_fd *fds, unsigned count, int timeout) { return ::poll(fds, count, timeout); }
bool poll_interrupted() { return errno == EINTR; }
#endif
using native_socket = ::asio::ip::udp::socket::native_handle_type;
// Both wake ends are non-blocking (set through Asio); a full buffer already wakes.
void wake_signal(native_socket socket) {
  const char byte = 0;
  (void)::send(socket, &byte, 1, 0);
}
void wake_drain(native_socket socket) {
  constexpr int capacity = 64;
  char bytes[capacity];
  while (::recv(socket, bytes, capacity, 0) > 0) {
  }
}
thread_local wl_udp *tls_owner_service = nullptr;
struct service_guard {
  wl_udp *previous;
  explicit service_guard(wl_udp *self) noexcept : previous(tls_owner_service) {
    tls_owner_service = self;
  }
  ~service_guard() { tls_owner_service = previous; }
};
#if defined(_WIN32)
using socklen_arg = int;
int socket_error() { return ::WSAGetLastError(); }
constexpr int peer_unreachable = WSAECONNRESET;
#else
using socklen_arg = socklen_t;
int socket_error() { return errno; }
constexpr int peer_unreachable = ECONNREFUSED;
#endif
}
struct wl_udp : wl_transport_t {
  explicit wl_udp(const wl_transport_ops_t *operations)
      : wl_transport_t{operations}, socket(io), wake_socket(io) {}
  wl_err_t attach(const wl_transport_attach_t &);

  wl_err_t start(const wl_udp_config_t &config, std::error_code &error);
  wl_err_t set_peer(const char *address, uint16_t port_number);
  void stop();
  wl_err_t service();
  bool ready() const;
  wl_err_t wait_for_activity(std::chrono::milliseconds);
  uint16_t local_port() const;
  wl_packet_submit_t submit(const uint8_t *data, size_t length);
  void notify();

  wl_packet_port_t *port{};
  wl_udp_stats_t stats{};

private:
  wl_err_t set_peer(const ::asio::ip::udp::endpoint &endpoint, std::error_code &error,
                    bool learned = false);
  void reject_rx();
  wl_err_t publish_rx(size_t received, const ::asio::ip::udp::endpoint *source);

  ::asio::io_context io;
  ::asio::ip::udp::socket socket;
  ::asio::ip::udp::socket wake_socket; // connected to itself; only native send/recv
  native_socket data_handle{}, wake_handle{};
  ::asio::ip::udp::endpoint peer;
  bool connected{}, verify_source_until_empty{};
  size_t maximum_datagram_size{}, maximum_tx_datagram_size{}, maximum_rx_frame{};
  unsigned service_budget{};
  bool learn_peer{};
  wl_err_t (*decode)(uint8_t *, size_t, size_t *){};
  wl_packet_claim_t rx_claim{};
  bool rx_claimed{};
  std::atomic<bool> wake{false}, waiting{false};
  bool have_peer{}, started{}, tx_blocked{}, more_rx{};
};

wl_err_t wl_udp::start(const wl_udp_config_t &config, std::error_code &error) {
  error.clear();
  if (config.maximum_datagram_size > 65507 || config.maximum_tx_datagram_size > 65507 ||
      config.service_budget > 16 || (config.peer_address == nullptr) != (config.peer_port == 0))
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
  // Let bursts wait in the data socket while the owner drains its RX slots.
  // This is a capacity hint; the OS may cap it or keep its default.
  constexpr int burst_buffer_bytes = 512 * 1024;
  ::asio::socket_base::receive_buffer_size buffer;
  std::error_code hint_error;
  pending_socket.get_option(buffer, hint_error);
  if (!hint_error && buffer.value() < burst_buffer_bytes)
    pending_socket.set_option(::asio::socket_base::receive_buffer_size(burst_buffer_bytes), hint_error);
  pending_socket.bind({local, config.bind_port}, error);
  if (!error) pending_socket.non_blocking(true, error);
  if (error) return WL_ERR_IO;

  ::asio::ip::udp::socket pending_wake(io);
  pending_wake.open(::asio::ip::udp::v4(), error);
  if (!error) pending_wake.bind({::asio::ip::address_v4::loopback(), 0}, error);
  if (!error) pending_wake.connect(pending_wake.local_endpoint(error), error);
  if (!error) pending_wake.non_blocking(true, error);
  if (error) return WL_ERR_IO;
  wake_socket = std::move(pending_wake);
  wake_handle = wake_socket.native_handle();

  socket = std::move(pending_socket);
  data_handle = socket.native_handle();
  maximum_datagram_size = config.maximum_datagram_size;
  maximum_tx_datagram_size = config.maximum_tx_datagram_size;
  service_budget = config.service_budget ? config.service_budget : 8;
  learn_peer = config.learn_peer;
  decode = config.decode;
  if (config.peer_address) return set_peer(initial_peer, error);
  return WL_OK;
}

wl_err_t wl_udp::attach(const wl_transport_attach_t &info) {
  if (started || !socket.is_open()) return WL_ERR_INVALID_STATE;
  const size_t rx = maximum_datagram_size ? maximum_datagram_size : info.rx_wire_max;
  const size_t tx = maximum_tx_datagram_size ? maximum_tx_datagram_size : info.tx_wire_max;
  if ((info.stream && !decode) || !info.port || rx == 0 || rx > info.rx_wire_max ||
      rx >= wl_packet_rx_unit_size(info.port) || rx > 65507 || tx > 65507)
    return WL_ERR_INVALID_ARG;
  maximum_rx_frame = info.stream ? info.stream->config.maximum_rx_frame_size : info.rx_wire_max;
  maximum_datagram_size = rx;
  maximum_tx_datagram_size = tx;
  port = info.port;
  started = true;
  return WL_OK;
}

wl_err_t wl_udp::set_peer(const ::asio::ip::udp::endpoint &endpoint, std::error_code &error,
                          bool learned) {
  if (!connected || peer != endpoint) {
#if defined(__APPLE__) || defined(__FreeBSD__)
    if (connected) {
      sockaddr unspec{};
      unspec.sa_len = sizeof(unspec);
      unspec.sa_family = AF_UNSPEC;
      // BSD may disconnect successfully, then reject the unspecified address.
      if (::connect(data_handle, &unspec, sizeof(unspec)) < 0 &&
          errno != EAFNOSUPPORT && errno != EINVAL) {
        error = std::error_code(errno, std::system_category());
        return WL_ERR_IO;
      }
      connected = false;
    }
#endif
    socket.connect(endpoint, error);
    if (error && !learned) {
#if defined(__APPLE__) || defined(__FreeBSD__)
      if (have_peer) {
        std::error_code restore;
        socket.connect(peer, restore);
        connected = !restore; // Otherwise keep using the old peer via sendto.
        verify_source_until_empty = connected;
      }
#endif
      return WL_ERR_IO;
    }
    // A learned sender remains usable via sendto if connect failed.
    peer = endpoint;
    have_peer = true;
    connected = !error;
    // connect does not remove datagrams already queued from other sources.
    verify_source_until_empty = connected;
  }
  tx_blocked = false;
  if (port) wl_packet_activity(port);
  return WL_OK;
}

wl_err_t wl_udp::set_peer(const char *address, uint16_t port_number) {
  if (!socket.is_open() || !port_number) return WL_ERR_INVALID_STATE;
  std::error_code error;
  const auto value = ::asio::ip::make_address(address, error);
  if (error) return WL_ERR_INVALID_ARG;
  const auto local = socket.local_endpoint(error);
  if (error) return WL_ERR_IO;
  if (value.is_v6() != local.address().is_v6()) return WL_ERR_INVALID_ARG;
  return set_peer({value, port_number}, error);
}

wl_packet_submit_t wl_udp::submit(const uint8_t *data, size_t length) {
  if (!data || !length || !started || length > maximum_tx_datagram_size)
    return WL_PACKET_FAILED;
  if (!have_peer)
    return WL_PACKET_BUSY;
  const auto *bytes = reinterpret_cast<const char *>(data);
  const int count = static_cast<int>(length); // One UDP datagram, at most 65507 bytes.
  bool retried = false;
  for (;;) {
    const auto sent = connected ? ::send(data_handle, bytes, count, 0)
                                : ::sendto(data_handle, bytes, count, 0, peer.data(),
                                           static_cast<socklen_arg>(peer.size()));
    if (sent == count) break;
    if (sent < 0) {
      const int err = socket_error();
      if (err == ::asio::error::interrupted) continue;
      if (connected && err == peer_unreachable) {
        // A previous datagram hit a closed peer port. Try this datagram once more.
        if (!retried) { retried = true; continue; }
        tx_blocked = false;
        return WL_PACKET_SENT; // Best-effort drop, no outstanding buffer.
      }
      if (err == ::asio::error::would_block || err == ::asio::error::try_again ||
          err == ::asio::error::no_buffer_space) {
        tx_blocked = true;
        return WL_PACKET_BUSY;
      }
    }
    ++stats.errors;
    return WL_PACKET_FAILED;
  }
  ++stats.sent;
  tx_blocked = false;
  return WL_PACKET_SENT; // Whole datagram accepted; no retained user memory.
}

void wl_udp::reject_rx() {
  (void)wl_packet_rx_abort(port, &rx_claim);
  rx_claimed = false;
  ++stats.rejected;
}

wl_err_t wl_udp::publish_rx(size_t received, const ::asio::ip::udp::endpoint *source) {
  if (decode && (decode(rx_claim.span.data, received, &received) != WL_OK || received > maximum_rx_frame)) {
    reject_rx();
    return WL_OK;
  }
  if (!have_peer) {
    wl_wire_frame_t frame{};
    if (!learn_peer || !source ||
        wl_wire_native_none_read(rx_claim.span.data, received, &frame) != WL_OK) {
      reject_rx();
      return WL_OK;
    }
    std::error_code connect_error;
    set_peer(*source, connect_error, true);
  }
  const wl_err_t result = wl_packet_rx_commit(port, &rx_claim, received);
  if (result != WL_OK) {
    (void)wl_packet_rx_abort(port, &rx_claim);
    rx_claimed = false;
    return result;
  }
  rx_claimed = false;
  ++stats.received;
  return WL_OK;
}

wl_err_t wl_udp::service() {
  if (!started)
    return WL_ERR_INVALID_STATE;
  const service_guard guard(this);
  more_rx = false;
  for (unsigned n = 0; n < service_budget; ++n) {
    if (!rx_claimed) {
      const wl_err_t result = wl_packet_rx_claim(port, maximum_datagram_size + 1, &rx_claim);
      if (result == WL_ERR_WOULD_BLOCK) {
        more_rx = true; // Datagrams may remain queued until a unit is released.
        return WL_OK;
      }
      if (result != WL_OK)
        return result;
      rx_claimed = true;
    }
    auto &claim = rx_claim;
    const bool check_source = !connected || verify_source_until_empty;
    int err = 0;
    size_t received = 0;
    if (!check_source) {
#if defined(_WIN32)
      std::error_code error;
      received = socket.receive(::asio::buffer(claim.span.data, claim.span.length), 0, error);
      err = error.value();
#else
      ssize_t nbytes;
      do {
        nbytes = ::recv(data_handle, claim.span.data, claim.span.length, 0);
      } while (nbytes < 0 && errno == EINTR);
      err = nbytes < 0 ? errno : 0;
      received = nbytes >= 0 ? static_cast<size_t>(nbytes) : 0U;
#endif
      if (err == ::asio::error::would_block || err == ::asio::error::try_again)
        return WL_OK; // Keep the idle RX reservation.
      if (err == peer_unreachable) continue; // Bounded by service_budget.
      if (err && err != ::asio::error::message_size) {
        (void)wl_packet_rx_abort(port, &claim);
        rx_claimed = false;
        ++stats.errors;
        return WL_ERR_IO;
      }
      if (err || !received || received > maximum_datagram_size) {
        reject_rx();
        continue;
      }
      const wl_err_t committed = publish_rx(received, nullptr);
      if (committed != WL_OK) return committed;
    } else {
      ::asio::ip::udp::endpoint source;
#if defined(_WIN32)
      std::error_code error;
      received =
          socket.receive_from(::asio::buffer(claim.span.data, claim.span.length), source, 0, error);
      err = error.value();
#else
      socklen_t source_len = static_cast<socklen_t>(source.capacity());
      ssize_t nbytes;
      do {
        nbytes = ::recvfrom(data_handle, claim.span.data, claim.span.length, 0, source.data(),
                            &source_len);
      } while (nbytes < 0 && errno == EINTR);
      err = nbytes < 0 ? errno : 0;
      if (nbytes >= 0) {
        received = static_cast<size_t>(nbytes);
        if (static_cast<size_t>(source_len) <= source.capacity())
          source.resize(static_cast<size_t>(source_len));
      }
#endif
      if (err == ::asio::error::would_block || err == ::asio::error::try_again) {
        verify_source_until_empty = false;
        return WL_OK; // Keep the idle RX reservation.
      }
      if (connected && err == peer_unreachable) continue; // Bounded by service_budget.
      if (err && err != ::asio::error::message_size) {
        (void)wl_packet_rx_abort(port, &claim);
        rx_claimed = false;
        ++stats.errors;
        return WL_ERR_IO;
      }
      if (err || !received || received > maximum_datagram_size ||
          (have_peer && source != peer)) {
        reject_rx();
        continue;
      }
      const wl_err_t committed = publish_rx(received, &source);
      if (committed != WL_OK) return committed;
    }
  }
  more_rx = true;
  return WL_OK;
}
// Readiness reports only known pending work; the kernel queue is observed by
// wait_for_activity's poll, never by a per-check FIONREAD.
bool wl_udp::ready() const {
  return started && more_rx && (rx_claimed || wl_packet_rx_available(port));
}
void wl_udp::notify() {
  if (tls_owner_service == this) return;
  wake.store(true);
  if (waiting.load()) wake_signal(wake_handle);
}
wl_err_t wl_udp::wait_for_activity(std::chrono::milliseconds maximum) {
  if (!started)
    return WL_ERR_INVALID_STATE;
  if (maximum.count() < 0)
    return WL_ERR_INVALID_ARG;
  if (ready())
    return WL_OK;
  poll_fd fds[2]{};
  fds[0].fd = wake_handle;
  fds[0].events = POLLIN;
  fds[1].fd = data_handle;
  fds[1].events = static_cast<short>(((rx_claimed || wl_packet_rx_available(port)) ? POLLIN : 0) |
                                     (tx_blocked ? POLLOUT : 0));
  const unsigned count = fds[1].events ? 2U : 1U;
  const int timeout = maximum.count() >= static_cast<long long>(UINT32_MAX) ? -1
                      : maximum.count() > INT_MAX ? INT_MAX : static_cast<int>(maximum.count());
  // Dekker pairing with notify(): either the notifier sees `waiting`, or this
  // owner sees `wake` before blocking.
  waiting.store(true, std::memory_order_seq_cst);
  bool woken = wake.load(std::memory_order_relaxed);
  if (woken) woken = wake.exchange(false, std::memory_order_acq_rel);
  const int polled = woken ? 0 : poll_sockets(fds, count, timeout);
  waiting.store(false, std::memory_order_release);
  if (polled < 0 && !poll_interrupted())
    return WL_ERR_IO;
  if (polled > 0 && fds[0].revents)
    wake_drain(wake_handle);
  if (!woken && wake.load(std::memory_order_acquire))
    woken = wake.exchange(false);
  const bool io_ready = polled > 0 && count == 2U && fds[1].revents != 0;
  if (!woken && !io_ready)
    return polled < 0 ? WL_OK : WL_ERR_NO_DATA; // EINTR is a spurious wake.
  if (io_ready)
    more_rx = true;
  if (tx_blocked) {
    tx_blocked = false;
    wl_packet_activity(port);
  }
  return WL_OK;
}
void wl_udp::stop() {
  if (!started)
    return;
  started = false;
  if (rx_claimed) {
    (void)wl_packet_rx_abort(port, &rx_claim);
    rx_claimed = false;
  }
  port = nullptr;
  notify();
  std::error_code error;
  socket.close(error);
}
uint16_t wl_udp::local_port() const {
  std::error_code error;
  const auto endpoint = socket.local_endpoint(error);
  return error ? 0 : endpoint.port();
}

namespace {
const wl_transport_ops_t udp_ops{
  nullptr,
  [](wl_transport_t *self, const wl_transport_attach_t *info) {
    return static_cast<wl_udp *>(self)->attach(*info);
  },
  [](wl_transport_t *self, const uint8_t *data, size_t length, wl_packet_token_t) -> wl_packet_submit_t {
    try { return static_cast<wl_udp *>(self)->submit(data, length); }
    catch (...) { return WL_PACKET_FAILED; }
  },
  [](wl_transport_t *self) { static_cast<wl_udp *>(self)->notify(); },
  [](wl_transport_t *self, uint32_t) -> wl_err_t {
    try { return static_cast<wl_udp *>(self)->service(); }
    catch (const std::bad_alloc &) { return WL_ERR_NO_MEM; }
    catch (...) { return WL_ERR_IO; }
  },
  [](const wl_transport_t *self, uint32_t) {
    return wl_io_readiness_at(static_cast<const wl_udp *>(self)->ready() ? 0U : UINT32_MAX);
  },
  [](wl_transport_t *self, uint32_t limit) -> wl_err_t {
    try { return static_cast<wl_udp *>(self)->wait_for_activity(std::chrono::milliseconds(limit)); }
    catch (const std::bad_alloc &) { return WL_ERR_NO_MEM; }
    catch (...) { return WL_ERR_IO; }
  },
  [](wl_transport_t *self) -> wl_err_t {
    static_cast<wl_udp *>(self)->stop();
    return WL_OK;
  }
};
}

wl_err_t wl_udp_open(wl_udp_t **out, const wl_udp_config_t *config, int *os_error) {
  if (os_error) *os_error = 0;
  if (!out) return WL_ERR_INVALID_ARG;
  *out = nullptr;
  if (!config) return WL_ERR_INVALID_ARG;
  try {
    auto udp = std::make_unique<wl_udp>(&udp_ops);
    std::error_code error;
    const auto status = udp->start(*config, error);
    if (status == WL_OK) *out = udp.release();
    else if (os_error && status == WL_ERR_IO) *os_error = error.value();
    return status;
  } catch (const std::bad_alloc &) { return WL_ERR_NO_MEM; }
    catch (const std::system_error &error) {
      if (os_error) *os_error = error.code().value();
      return WL_ERR_IO;
    } catch (...) { return WL_ERR_IO; }
}

wl_transport_t *wl_udp_transport(wl_udp_t *udp) { return udp; }
void wl_udp_close(wl_udp_t *udp) { delete udp; }

wl_err_t wl_udp_set_peer(wl_udp_t *udp, const char *address, uint16_t port) {
  if (!udp || !address || !port) return WL_ERR_INVALID_ARG;
  try { return udp->set_peer(address, port); }
  catch (const std::bad_alloc &) { return WL_ERR_NO_MEM; }
  catch (...) { return WL_ERR_IO; }
}
uint16_t wl_udp_local_port(const wl_udp_t *udp) { return udp ? udp->local_port() : 0; }
wl_udp_stats_t wl_udp_stats(const wl_udp_t *udp) { return udp ? udp->stats : wl_udp_stats_t{}; }
