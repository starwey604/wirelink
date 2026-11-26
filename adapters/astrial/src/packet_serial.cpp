/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/astrial/packet_serial.hpp"
#include "wirelink/detail/coalescing_event.hpp"
#include <atomic>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>
namespace wirelink::astrial {
class PacketSerialAdapter::Impl {
public:
  Impl(PacketSerialConfig value, wl_span_t storage)
      : config(std::move(value)), fifo_storage(storage) {}
  std::span<uint8_t> acquire() {
    if (stopping.load(std::memory_order_acquire) || fault.load(std::memory_order_acquire))
      return {};
    if (reserved) {
      fail();
      return {};
    }
    wl_span_t span{};
    auto error = wl_byte_fifo_reserve(&fifo, &span);
    if (error == WL_ERR_WOULD_BLOCK) {
      if (!paused.exchange(true, std::memory_order_acq_rel))
        activity.notify();
      return {};
    }
    if (error != WL_OK) {
      fail();
      return {};
    }
    capacity = span.length;
    reserved = true;
    paused.store(false, std::memory_order_release);
    return {span.data, span.length};
  }
  void received(const std::error_code &error, size_t length) {
    if (!reserved || length > capacity) {
      fail();
      return;
    }
    reserved = false;
    capacity = 0;
    if (wl_byte_fifo_commit(&fifo, length) != WL_OK) {
      fail();
      return;
    }
    if (error && !stopping.load(std::memory_order_acquire)) {
      wl_byte_fifo_note_gap(&fifo);
      fail();
    }
  }
  void fail() {
    fault.store(true, std::memory_order_release);
    ++errors;
    activity.notify();
  }
  void written(wl_packet_token_t completed_token, size_t expected, const std::error_code &error,
               size_t length) {
    uint32_t state = (completed_token.sequence << 2U) | 1U;
    const bool success = !error && length == expected;
    const bool poll_drain = success && config.drained;
    const uint32_t next = poll_drain ? (completed_token.sequence << 2U) | 2U : 0U;
    if (!tx.compare_exchange_strong(state, next, std::memory_order_acq_rel))
      return;
    if (!success && !stopping.load(std::memory_order_acquire))
      fail();
    const auto event = !success ? WL_PACKET_TX_ERROR :
                       poll_drain ? WL_PACKET_CONSUMED : WL_PACKET_DRAINED;
    (void)wl_packet_tx_publish(port, completed_token, event);
  }
  wl_packet_port_t *port{};
  wl_byte_fifo_t fifo{};
  wl_packet_stream_t *stream{};
  std::vector<uint8_t> owned_fifo;
  std::unique_ptr<Serial> serial;
  PacketSerialConfig config;
  wl_span_t fifo_storage;
  std::atomic<bool> stopping{false}, fault{false}, paused{false};
  std::atomic<uint32_t> tx{};
  std::atomic<uint64_t> errors{};
  wl_packet_token_t token{}; // Owner only, callback captures its own copy.
  uint32_t drain_due{};
  bool drain_armed{};
  bool started{}, started_once{}, reserved{}; // reserved/capacity: producer, or owner after join.
  size_t capacity{};
  wirelink::detail::CoalescingEvent activity;
};
PacketSerialAdapter::PacketSerialAdapter(PacketSerialConfig config, wl_span_t fifo_storage)
    : wl_transport_t{}, impl_(std::make_unique<Impl>(std::move(config), fifo_storage)) {
  static const wl_transport_ops_t operations = {
      nullptr,
      [](wl_transport_t *t, const wl_transport_attach_t *info) -> wl_err_t {
        auto *self = static_cast<PacketSerialAdapter *>(t);
        try { return self->attach(*info); }
        catch (const std::bad_alloc &) { self->stop(); return WL_ERR_NO_MEM; }
        catch (const std::system_error &) { self->stop(); return WL_ERR_IO; }
      },
      [](wl_transport_t *t, const uint8_t *bytes, size_t length, wl_packet_token_t token) {
        return submit(static_cast<PacketSerialAdapter *>(t), bytes, length, token);
      },
      [](wl_transport_t *t) { notify(static_cast<PacketSerialAdapter *>(t)); },
      [](wl_transport_t *t, uint32_t now) { return static_cast<PacketSerialAdapter *>(t)->service(now); },
      [](const wl_transport_t *t, uint32_t now) { return static_cast<const PacketSerialAdapter *>(t)->readiness(now); },
      [](wl_transport_t *t, uint32_t ms) -> wl_err_t {
        auto &event = static_cast<PacketSerialAdapter *>(t)->impl_->activity;
        if (ms == UINT32_MAX) { event.wait(); return WL_OK; }
        return event.wait_for(std::chrono::milliseconds(ms)) ? WL_OK : WL_ERR_NO_DATA;
      },
      [](wl_transport_t *t) -> wl_err_t { static_cast<PacketSerialAdapter *>(t)->stop(); return WL_OK; }
  };
  ops = &operations;
}
PacketSerialAdapter::PacketSerialAdapter(PacketSerialConfig config, size_t fifo_size)
    : PacketSerialAdapter(std::move(config), wl_span_t{}) {
  impl_->owned_fifo.resize(fifo_size);
  impl_->fifo_storage = {impl_->owned_fifo.data(), impl_->owned_fifo.size()};
}
PacketSerialAdapter::~PacketSerialAdapter() { stop(); }
wl_err_t PacketSerialAdapter::attach(const wl_transport_attach_t &layout) {
  auto &p = *impl_;
  if (p.started_once) return WL_ERR_INVALID_STATE;
  const auto &c = p.config;
  if (c.port.empty() || (c.drained && (!c.drain_check_ms || c.drain_check_ms > INT32_MAX)) ||
      layout.framing != WL_NODE_COBS_CRC16 || !layout.stream)
    return WL_ERR_INVALID_ARG;
  p.port = layout.port;
  p.stream = layout.stream;
  const auto error = wl_byte_fifo_init(&p.fifo, p.fifo_storage, notify, this);
  if (error != WL_OK) return error;
  auto opened = Serial::builder()
                    .baud_rate(c.baud_rate)
                    .parity(c.parity)
                    .stop_bits(c.stop_bits)
                    .data_bits(c.data_bits)
                    .auto_reconnect(false)
                    .open(c.port);
  if (!opened) return WL_ERR_IO;
  p.serial = std::make_unique<Serial>(std::move(*opened));
  p.started = p.started_once = true;
  p.serial->on_disconnect([&p](const std::error_code &) {
    if (!p.stopping.load(std::memory_order_acquire))
      p.fail();
  });
  p.serial->on_data_borrowed([&p] { return p.acquire(); },
                             [&p](const std::error_code &e, size_t n) { p.received(e, n); });
  return WL_OK;
}
wl_packet_submit_t PacketSerialAdapter::submit(void *user, const uint8_t *data, size_t length,
                                               wl_packet_token_t token) {
  if (!user || !data || !length || !token.sequence || token.sequence > WL_PACKET_SEQUENCE_MAX)
    return WL_PACKET_FAILED;
  auto &p = *static_cast<PacketSerialAdapter *>(user)->impl_;
  if (!p.started || p.stopping.load(std::memory_order_acquire) ||
      p.fault.load(std::memory_order_acquire))
    return WL_PACKET_FAILED;
  uint32_t none = 0;
  if (!p.tx.compare_exchange_strong(none, (token.sequence << 2U) | 1U, std::memory_order_acq_rel))
    return WL_PACKET_BUSY;
  p.token = token;
  p.drain_armed = false;
  try {
    p.serial->async_write_borrowed({data, length},
        [&p, token, length](const std::error_code &e, size_t n) { p.written(token, length, e, n); });
  } catch (const std::exception &) {
    p.tx.store(0, std::memory_order_release);
    p.fail();
    return WL_PACKET_FAILED;
  }
  return WL_PACKET_STARTED;
}
void PacketSerialAdapter::notify(void *user) {
  if (user)
    static_cast<PacketSerialAdapter *>(user)->impl_->activity.notify();
}
wl_err_t PacketSerialAdapter::service(uint32_t now) {
  auto &p = *impl_;
  if (!p.started || p.stopping.load(std::memory_order_acquire))
    return WL_ERR_INVALID_STATE;
  auto state = p.tx.load(std::memory_order_acquire);
  if ((state & 3U) == 2U && (!p.drain_armed || (int32_t)(now - p.drain_due) >= 0)) {
    int drained = p.config.drained(p.config.drain_user);
    if (drained != 0) {
      p.tx.store(0, std::memory_order_release);
      if (drained < 0)
        p.fail();
      (void)wl_packet_tx_publish(p.port, p.token,
                                 drained > 0 ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR);
    } else {
      p.drain_due = now + p.config.drain_check_ms;
      p.drain_armed = true;
    }
  }
  int input = wl_byte_fifo_to_cobs(&p.fifo, p.stream, 16);
  if (p.paused.load(std::memory_order_acquire) && wl_byte_fifo_has_space(&p.fifo) &&
      p.paused.exchange(false, std::memory_order_acq_rel))
    p.serial->resume_read();
  if (p.fault.load(std::memory_order_acquire))
    return WL_ERR_IO;
  return input == WL_ERR_WOULD_BLOCK ? WL_OK : input;
}
wl_io_readiness_t PacketSerialAdapter::readiness(uint32_t now) const {
  const auto &p = *impl_;
  auto r = wl_io_readiness_idle();
  if (!p.started)
    return r;
  r.runnable = p.fault.load(std::memory_order_acquire) ||
               wl_byte_fifo_cobs_ready(&p.fifo, p.stream) ||
               (p.paused.load(std::memory_order_acquire) && wl_byte_fifo_has_space(&p.fifo));
  if ((p.tx.load(std::memory_order_acquire) & 3U) == 2U)
    wl_io_readiness_merge(&r, wl_io_readiness_at(!p.drain_armed || (int32_t)(now - p.drain_due) >= 0
                                                     ? 0U
                                                     : p.drain_due - now));
  return r;
}
bool PacketSerialAdapter::wait_for_activity(std::chrono::nanoseconds timeout) {
  return impl_->activity.wait_for(timeout);
}
void PacketSerialAdapter::stop() {
  auto &p = *impl_;
  if (!p.started)
    return;
  p.stopping.store(true, std::memory_order_release);
  wl_packet_stop(p.port);
  p.serial->close(); // Cancels and joins the borrowed read/write callbacks.
  if (p.reserved) {
    (void)wl_byte_fifo_commit(&p.fifo, 0);
    p.reserved = false;
  }
  if (p.tx.exchange(0, std::memory_order_acq_rel))
    (void)wl_packet_tx_publish(p.port, p.token, WL_PACKET_TX_ERROR);
  (void)wl_packet_stream_abort_rx(p.stream);
  p.started = false;
  p.activity.notify();
}
uint64_t PacketSerialAdapter::errors() const {
  return impl_->errors.load(std::memory_order_relaxed);
}
} // namespace wirelink::astrial
