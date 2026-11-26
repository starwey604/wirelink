/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/astrial/packet_usb.hpp"
#include "wirelink/detail/coalescing_event.hpp"
#include <atomic>
#include <new>
#include <system_error>
#include <utility>
namespace wirelink::astrial {
class PacketUsbAdapter::Impl {
public:
  explicit Impl(PacketUsbConfig value) : config(std::move(value)) {}
  void wake() {
    if (owner_driven) {
      activity_latched.store(true, std::memory_order_release);
      return;
    }
    activity.notify();
    if (config.wake) config.wake(config.wake_user);
  }
  bool wait_impl(std::chrono::nanoseconds timeout, bool infinite) {
    if (!owner_driven) {
      if (infinite) {
        activity.wait();
        return true;
      }
      return activity.wait_for(timeout);
    }
    if (activity_latched.exchange(false, std::memory_order_acq_rel)) return true;
    if (is_stopped.load(std::memory_order_acquire) || !device) return false;
    if (infinite) {
      while (!is_stopped.load(std::memory_order_acquire)) {
        auto res = device->poll_events(std::chrono::milliseconds{1000});
        if (!res || *res || activity_latched.load(std::memory_order_acquire)) break;
      }
      activity_latched.store(false, std::memory_order_relaxed);
      polled_in_wait = true;
      return true;
    }
    const auto us = timeout <= std::chrono::nanoseconds::zero()
        ? std::chrono::microseconds{0}
        : std::chrono::duration_cast<std::chrono::microseconds>(
              timeout + std::chrono::nanoseconds{999});
    auto res = device->poll_events(us);
    const bool completed = res && *res;
    const bool latched = activity_latched.exchange(false, std::memory_order_acq_rel);
    polled_in_wait = true;
    return completed || latched;
  }
  void fail(PacketUsbFault reason) {
    auto expected = PacketUsbFault::none;
    if (fault.compare_exchange_strong(expected, reason, std::memory_order_acq_rel)) {
      ++errors;
      fault_pending.store(true, std::memory_order_release);
      wl_packet_activity(port);
    }
  }
  UsbBorrowedBuffer acquire() {
    if (stopping.load(std::memory_order_acquire) || fault.load() != PacketUsbFault::none)
      return {};
    if (claimed) { fail(PacketUsbFault::invalid_completion); return {}; }
    wl_packet_claim_t next{};
    auto error = wl_packet_rx_claim(port, read_size, &next);
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
      (void)wl_packet_rx_abort(port, &claim);
      claimed = false;
      if (!cancel && error) fail(PacketUsbFault::transfer);
      else if (length >= claim.span.length) fail(PacketUsbFault::boundary_lost);
      return;
    }
    if (length > maximum_frame) {
      (void)wl_packet_rx_abort(port, &claim);
      claimed = false;
      ++errors;
      return;
    }
    auto result = wl_packet_rx_commit(port, &claim, length);
    claimed = false;
    if (result != WL_OK) { fail(PacketUsbFault::invalid_completion); return; }
    rx_bytes.fetch_add(length, std::memory_order_relaxed);
    rx_units.fetch_add(1, std::memory_order_relaxed);
  }
  void written(uint32_t sequence, const std::error_code &error, size_t length) {
    if (tx_sequence.load(std::memory_order_acquire) != sequence) {
      ++stale;
      return;
    }
    // Write callbacks are serialized. Snapshot the lease before reopening admission.
    const auto token = tx_token;
    const bool success = !error && tx_length == length;
    tx_sequence.store(0, std::memory_order_release);
    const bool cancel = stopping.load(std::memory_order_acquire) &&
        error == make_error_code(UsbError::TransferCancelled);
    if (success) { tx_bytes.fetch_add(length, std::memory_order_relaxed); ++tx_units; }
    else if (!cancel) ++errors;
    // Clear adapter admission before publication wakes an owner/new submit.
    if (wl_packet_tx_publish(port, token,
        success ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR) != WL_OK) ++stale;
  }
  static constexpr size_t rx_alignment = 512;
  struct RxDeleter {
    void operator()(uint8_t *p) const {
      ::operator delete(p, std::align_val_t{rx_alignment});
    }
  };
  wl_err_t configure_rx(size_t minimum, wl_transport_rx_override_t *out) {
    if (started_once) return WL_ERR_INVALID_STATE;
    if (config.rx.rx_storage) {
      *out = config.rx;
    } else if (!out->rx_storage || out->rx_unit_size < minimum ||
               out->rx_unit_size % rx_alignment != 0) {
      // The node supplies a 16-bit wire bound, plus one byte for overflow detection.
      const size_t unit = (minimum + rx_alignment - 1) & ~(rx_alignment - 1);
      const size_t bytes = WL_PACKET_RX_SLOTS_MAX * unit;
      owned_rx.reset(static_cast<uint8_t *>(
          ::operator new(bytes, std::align_val_t{rx_alignment}, std::nothrow)));
      if (!owned_rx) return WL_ERR_NO_MEM;
      *out = {owned_rx.get(), bytes, unit};
    }
    return WL_OK;
  }
  std::unique_ptr<uint8_t, RxDeleter> owned_rx;
  wl_packet_port_t *port{};
  std::unique_ptr<UsbBulkDevice> device;
  PacketUsbConfig config;
  size_t read_size{}, maximum_frame{};
  bool claimed{}; // I/O thread only, or owner after join.
  bool started_once{};
  bool owner_driven{};
  bool polled_in_wait{};
  wl_packet_claim_t claim{};
  std::atomic<bool> stopping{false}, is_stopped{true}, paused{false}, fault_pending{false};
  std::atomic<bool> activity_latched{false};
  std::atomic<PacketUsbFault> fault{PacketUsbFault::none};
  std::atomic<uint32_t> tx_sequence{};
  wl_packet_token_t tx_token{};
  size_t tx_length{};
  std::atomic<uint64_t> rx_bytes{}, tx_bytes{}, rx_units{}, tx_units{}, errors{}, stale{};
  wirelink::detail::CoalescingEvent activity;
};
PacketUsbAdapter::PacketUsbAdapter(PacketUsbConfig config)
    : wl_transport_t{}, impl_(std::make_unique<Impl>(std::move(config))) {
  static const wl_transport_ops_t operations = {
      [](wl_transport_t *t, size_t minimum, wl_transport_rx_override_t *out) -> wl_err_t {
        return static_cast<PacketUsbAdapter *>(t)->impl_->configure_rx(minimum, out);
      },
      [](wl_transport_t *t, const wl_transport_attach_t *info) -> wl_err_t {
        auto *self = static_cast<PacketUsbAdapter *>(t);
        try { return self->attach(*info); }
        catch (const std::bad_alloc &) { self->stop(); return WL_ERR_NO_MEM; }
        catch (const std::system_error &) { self->stop(); return WL_ERR_IO; }
      },
      [](wl_transport_t *t, const uint8_t *bytes, size_t length, wl_packet_token_t token) {
        return submit(static_cast<PacketUsbAdapter *>(t), bytes, length, token);
      },
      [](wl_transport_t *t) { notify(static_cast<PacketUsbAdapter *>(t)); },
      [](wl_transport_t *t, uint32_t) { return static_cast<PacketUsbAdapter *>(t)->service(); },
      [](const wl_transport_t *t, uint32_t) { return wl_io_readiness_at(static_cast<const PacketUsbAdapter *>(t)->ready() ? 0U : UINT32_MAX); },
      [](wl_transport_t *t, uint32_t ms) -> wl_err_t {
        auto &p = *static_cast<PacketUsbAdapter *>(t)->impl_;
        if (ms == UINT32_MAX) {
          return p.wait_impl(std::chrono::nanoseconds::zero(), true) ? WL_OK : WL_ERR_NO_DATA;
        }
        return p.wait_impl(std::chrono::milliseconds(ms), false) ? WL_OK : WL_ERR_NO_DATA;
      },
      [](wl_transport_t *t) -> wl_err_t { static_cast<PacketUsbAdapter *>(t)->stop(); return WL_OK; }
  };
  ops = &operations;
}
PacketUsbAdapter::~PacketUsbAdapter() { stop(); }
wl_err_t PacketUsbAdapter::attach(const wl_transport_attach_t &layout) {
  auto &p = *impl_;
  if (p.started_once) return WL_ERR_INVALID_STATE;
  if (layout.framing != WL_NODE_NATIVE) return WL_ERR_INVALID_ARG;
  p.port = layout.port;
  p.read_size = wl_packet_rx_unit_size(p.port);
  p.maximum_frame = layout.rx_wire_max;
  if (!p.maximum_frame || p.maximum_frame >= p.read_size) return WL_ERR_INVALID_ARG;
  const auto &config = p.config;
  p.owner_driven = (config.wake == nullptr);
  p.polled_in_wait = false;
  p.activity_latched.store(false, std::memory_order_relaxed);
  auto usb = config.usb;
  usb.read_queue_depth = 1;
  usb.auto_reconnect = false;
  usb.run_event_thread = !p.owner_driven;
  auto opened = UsbBulkDevice::open(usb);
  if (!opened) return WL_ERR_IO;
  const auto info = opened->endpoint_info();
  if (!info.maximum_packet_size_in || !info.maximum_packet_size_out ||
      p.read_size % info.maximum_packet_size_in) return WL_ERR_INVALID_ARG;
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
  p.tx_token = token;
  p.tx_length = length;
  try {
    auto result = p.device->async_write_borrowed({data, length},
        [&p, sequence = token.sequence](const std::error_code &e, size_t n) {
          p.written(sequence, e, n);
        }, UsbWriteOptions{.append_zero_length_packet = true});
    if (result) return WL_PACKET_STARTED;
  } catch (const std::exception &) {
    p.fail(PacketUsbFault::transfer);
  }
  p.tx_sequence.store(0, std::memory_order_release);
  if (p.fault.load(std::memory_order_acquire) == PacketUsbFault::none) ++p.errors;
  return WL_PACKET_FAILED;
}
void PacketUsbAdapter::notify(void *user) {
  if (user) static_cast<PacketUsbAdapter *>(user)->impl_->wake();
}
wl_err_t PacketUsbAdapter::service() {
  auto &p = *impl_;
  if (p.owner_driven) {
    if (!p.is_stopped.load(std::memory_order_acquire) &&
        !p.stopping.load(std::memory_order_acquire) && p.device && !p.polled_in_wait) {
      (void)p.device->poll_events(std::chrono::microseconds{0});
    }
    p.polled_in_wait = false;
    p.activity_latched.store(false, std::memory_order_relaxed);
  } else {
    (void)p.activity.try_wait(); // Consume a wake even if the owner skipped wait().
  }
  p.fault_pending.store(false, std::memory_order_release);
  if (!p.is_stopped.load() && !p.stopping.load() && p.fault.load() == PacketUsbFault::none &&
      p.paused.load(std::memory_order_acquire) && wl_packet_rx_available(p.port) &&
      p.paused.exchange(false, std::memory_order_acq_rel)) p.device->resume_reads();
  return p.fault.load() == PacketUsbFault::none ? WL_OK : WL_ERR_IO;
}
bool PacketUsbAdapter::ready() const {
  const auto &p = *impl_;
  return !p.is_stopped.load() && (p.fault_pending.load(std::memory_order_acquire) ||
      (p.fault.load() == PacketUsbFault::none && p.paused.load(std::memory_order_acquire) &&
       wl_packet_rx_available(p.port)));
}
void PacketUsbAdapter::stop() {
  auto &p = *impl_;
  if (p.is_stopped.load(std::memory_order_acquire)) return;
  p.stopping.store(true, std::memory_order_release);
  wl_packet_stop(p.port);
  p.device->close(); // Synchronously joins and completes cancellations on owner.
  if (p.claimed) { (void)wl_packet_rx_abort(p.port, &p.claim); p.claimed = false; }
  p.is_stopped.store(true, std::memory_order_release);
  p.polled_in_wait = false;
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
  return impl_->wait_impl(timeout, false);
}
}
