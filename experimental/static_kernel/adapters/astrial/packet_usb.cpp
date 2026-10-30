/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/astrial/packet_usb.hpp"
#include "wirelink/detail/coalescing_event.hpp"
#include <atomic>
#include <utility>

namespace wirelink::astrial {
class PacketUsbAdapter::Impl {
public:
  explicit Impl(wl_packet_port_t &value) : port(value) {}
  void wake() {
    activity.notify();
    if (config.wake) config.wake(config.wake_user);
  }
  void fail(PacketUsbFault reason) {
    auto expected = PacketUsbFault::none;
    if (fault.compare_exchange_strong(expected, reason, std::memory_order_acq_rel)) {
      ++errors;
      fault_pending.store(true, std::memory_order_release);
      wl_packet_activity(&port);
    }
  }
  UsbBorrowedBuffer acquire() {
    if (stopping.load(std::memory_order_acquire) || fault.load() != PacketUsbFault::none)
      return {};
    if (claimed) { fail(PacketUsbFault::invalid_completion); return {}; }
    wl_packet_claim_t next{};
    auto error = wl_packet_rx_claim(&port, read_size, &next);
    if (error != WL_OK) {
      if (error != WL_ERR_WOULD_BLOCK) fail(PacketUsbFault::invalid_completion);
      if (!paused.exchange(true, std::memory_order_acq_rel)) wake();
      return {};
    }
    claim = next;
    claimed = true;
    paused.store(false, std::memory_order_release);
    return {{claim.span.data, claim.span.length}, claim.token.sequence};
  }
  void received(const std::error_code &error, UsbBorrowedBuffer buffer, size_t length) {
    // Reject an old callback without aborting a newer claim.
    if (!claimed || buffer.token != claim.token.sequence ||
        buffer.bytes.data() != claim.span.data || buffer.bytes.size() != claim.span.length) {
      ++stale;
      return;
    }
    const bool cancel = stopping.load(std::memory_order_acquire) &&
        error == make_error_code(UsbError::TransferCancelled);
    if (error || length >= claim.span.length || !length) {
      (void)wl_packet_rx_abort(&port, &claim);
      claimed = false;
      if (!cancel && error) fail(PacketUsbFault::transfer);
      else if (length >= claim.span.length) fail(PacketUsbFault::boundary_lost);
      return;
    }
    auto result = wl_packet_rx_commit(&port, &claim, length);
    claimed = false;
    if (result != WL_OK) { fail(PacketUsbFault::invalid_completion); return; }
    rx_bytes.fetch_add(length, std::memory_order_relaxed);
    rx_units.fetch_add(1, std::memory_order_relaxed);
  }
  void written(wl_packet_token_t token, size_t expected,
               const std::error_code &error, size_t length) {
    auto sequence = token.sequence;
    if (!tx_sequence.compare_exchange_strong(sequence, 0, std::memory_order_acq_rel)) {
      ++stale;
      return;
    }
    const bool success = !error && expected == length;
    const bool cancel = stopping.load(std::memory_order_acquire) &&
        error == make_error_code(UsbError::TransferCancelled);
    if (success) { tx_bytes.fetch_add(length, std::memory_order_relaxed); ++tx_units; }
    else if (!cancel) ++errors;
    // Clear adapter admission before publication wakes an owner/new submit.
    if (wl_packet_tx_publish(&port, token,
        success ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR) != WL_OK) ++stale;
  }
  wl_packet_port_t &port;
  std::unique_ptr<UsbBulkDevice> device;
  PacketUsbConfig config;
  size_t read_size{};
  bool claimed{}; // I/O thread only, or owner after join.
  bool started_once{};
  wl_packet_claim_t claim{};
  std::atomic<bool> stopping{false}, is_stopped{true}, paused{false}, fault_pending{false};
  std::atomic<PacketUsbFault> fault{PacketUsbFault::none};
  std::atomic<uint32_t> tx_sequence{};
  std::atomic<uint64_t> rx_bytes{}, tx_bytes{}, rx_units{}, tx_units{}, errors{}, stale{};
  wirelink::detail::CoalescingEvent activity;
};
PacketUsbAdapter::PacketUsbAdapter(wl_packet_port_t &port) : impl_(std::make_unique<Impl>(port)) {}
PacketUsbAdapter::~PacketUsbAdapter() { stop(); }
wl_err_t PacketUsbAdapter::start(const PacketUsbConfig &config) {
  auto &p = *impl_;
  if (p.started_once) return WL_ERR_INVALID_STATE;
  p.read_size = wl_packet_rx_unit_size(&p.port);
  if (!config.maximum_frame_size || config.maximum_frame_size >= p.read_size)
    return WL_ERR_INVALID_ARG;
  auto usb = config.usb;
  usb.read_queue_depth = 1;
  usb.auto_reconnect = false;
  auto opened = UsbBulkDevice::open(usb);
  if (!opened) return WL_ERR_IO;
  const auto info = opened->endpoint_info();
  if (!info.maximum_packet_size_in || !info.maximum_packet_size_out ||
      p.read_size % info.maximum_packet_size_in) return WL_ERR_INVALID_ARG;
  p.config = config;
  p.device = std::make_unique<UsbBulkDevice>(std::move(*opened));
  p.started_once = true;
  p.is_stopped.store(false, std::memory_order_release);
  p.device->on_disconnect([&p](const std::error_code &) {
    if (!p.stopping.load(std::memory_order_acquire)) p.fail(PacketUsbFault::disconnected);
  });
  auto result = p.device->start_reads_borrowed([&p] { return p.acquire(); },
      [&p](const std::error_code &e, UsbBorrowedBuffer b, size_t n) { p.received(e, b, n); });
  if (!result) { stop(); return WL_ERR_IO; }
  return WL_OK;
}
wl_packet_submit_t PacketUsbAdapter::submit(void *user, const uint8_t *data,
                                           size_t length, wl_packet_token_t token) {
  if (!user || !data || !length || !token.sequence) return WL_PACKET_FAILED;
  auto &p = *static_cast<PacketUsbAdapter *>(user)->impl_;
  if (p.is_stopped.load(std::memory_order_acquire) || p.stopping.load(std::memory_order_acquire) ||
      p.fault.load(std::memory_order_acquire) != PacketUsbFault::none) return WL_PACKET_FAILED;
  uint32_t none = 0;
  if (!p.tx_sequence.compare_exchange_strong(none, token.sequence, std::memory_order_acq_rel))
    return WL_PACKET_BUSY;
  auto result = p.device->async_write_borrowed({data, length},
      [&p, token, length](const std::error_code &e, size_t n) { p.written(token, length, e, n); });
  if (!result) {
    p.tx_sequence.store(0, std::memory_order_release);
    ++p.errors;
    return WL_PACKET_FAILED;
  }
  return WL_PACKET_STARTED;
}
void PacketUsbAdapter::notify(void *user) {
  if (user) static_cast<PacketUsbAdapter *>(user)->impl_->wake();
}
wl_err_t PacketUsbAdapter::service() {
  auto &p = *impl_;
  p.fault_pending.store(false, std::memory_order_release);
  if (!p.is_stopped.load() && !p.stopping.load() && p.fault.load() == PacketUsbFault::none &&
      p.paused.load(std::memory_order_acquire) && wl_packet_rx_available(&p.port) &&
      p.paused.exchange(false, std::memory_order_acq_rel)) p.device->resume_reads();
  return p.fault.load() == PacketUsbFault::none ? WL_OK : WL_ERR_IO;
}
bool PacketUsbAdapter::ready() const {
  const auto &p = *impl_;
  return !p.is_stopped.load() && (p.fault_pending.load(std::memory_order_acquire) ||
      (p.fault.load() == PacketUsbFault::none && p.paused.load(std::memory_order_acquire) &&
       wl_packet_rx_available(&p.port)));
}
void PacketUsbAdapter::stop() {
  auto &p = *impl_;
  if (p.is_stopped.load(std::memory_order_acquire)) return;
  p.stopping.store(true, std::memory_order_release);
  wl_packet_stop(&p.port);
  p.device->close(); // Synchronously joins and completes cancellations on owner.
  if (p.claimed) { (void)wl_packet_rx_abort(&p.port, &p.claim); p.claimed = false; }
  p.is_stopped.store(true, std::memory_order_release);
  p.wake();
}
bool PacketUsbAdapter::stopped() const { return impl_->is_stopped.load(std::memory_order_acquire); }
PacketUsbFault PacketUsbAdapter::fault() const { return impl_->fault.load(std::memory_order_acquire); }
PacketUsbStats PacketUsbAdapter::stats() const {
  const auto &p = *impl_;
  return {p.rx_bytes.load(), p.tx_bytes.load(), p.rx_units.load(), p.tx_units.load(),
          p.errors.load(), p.stale.load()};
}
UsbBulkEndpointInfo PacketUsbAdapter::endpoint_info() const {
  return impl_->device ? impl_->device->endpoint_info() : UsbBulkEndpointInfo{};
}
bool PacketUsbAdapter::wait_for_activity(std::chrono::nanoseconds timeout) {
  return impl_->activity.wait_for(timeout);
}
}
