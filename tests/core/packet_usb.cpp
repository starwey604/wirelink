/* SPDX-License-Identifier: Apache-2.0 */
/* Actual adapter and C port, controlled Astrial completion boundary. The
 * libusb/USB driver implementation is unchanged and is qualified separately. */
#include "wirelink/astrial/packet_usb.hpp"
#include <cassert>
#include <cstring>
#include <utility>

extern "C" wl_packet_port_t *packet_usb_test_storage();
extern "C" int packet_usb_test_init(uint64_t, wl_packet_sink_fn, wl_packet_notify_fn, void *);
namespace {
UsbBulkDevice::ReadBufferProvider provider;
UsbBulkDevice::ReadCallback read_done;
UsbBulkDevice::WriteCallback write_done;
std::function<void(const std::error_code &)> disconnected;
UsbBorrowedBuffer active;
std::span<const uint8_t> leased;
bool inline_write;
unsigned resumes;
void arm() { if (provider && active.bytes.empty()) active = provider(); }
void receive(size_t length, std::error_code error = {}) {
  auto buffer = std::exchange(active, {});
  assert(!buffer.bytes.empty());
  read_done(error, buffer, length);
  arm();
}
void transmit(std::error_code error = {}) {
  auto cb = std::exchange(write_done, {});
  assert(cb);
  auto n = leased.size(); leased = {};
  cb(error, error ? 0 : n);
}
}
class UsbBulkDevice::Impl {};
UsbBulkDevice::UsbBulkDevice(std::unique_ptr<Impl> value) : m_impl(std::move(value)) {}
UsbBulkDevice::~UsbBulkDevice() = default;
UsbBulkDevice::UsbBulkDevice(UsbBulkDevice &&) noexcept = default;
UsbBulkDevice &UsbBulkDevice::operator=(UsbBulkDevice &&) noexcept = default;
std::error_code make_error_code(UsbError e) { return {int(e), std::generic_category()}; }
tl::expected<UsbBulkDevice, std::error_code> UsbBulkDevice::open(const UsbBulkConfig &c) {
  assert(c.read_queue_depth == 1 && !c.auto_reconnect);
  return UsbBulkDevice(std::make_unique<Impl>());
}
UsbBulkEndpointInfo UsbBulkDevice::endpoint_info() const noexcept { return {64, 64}; }
tl::expected<void, std::error_code> UsbBulkDevice::start_reads_borrowed(
    ReadBufferProvider p, ReadCallback c) {
  provider = std::move(p); read_done = std::move(c); arm(); return {};
}
void UsbBulkDevice::resume_reads() { ++resumes; arm(); }
void UsbBulkDevice::on_disconnect(std::function<void(const std::error_code &)> c) {
  disconnected = std::move(c);
}
void UsbBulkDevice::close() {
  provider = {};
  if (!active.bytes.empty()) receive(0, make_error_code(UsbError::TransferCancelled));
  if (write_done) transmit(make_error_code(UsbError::TransferCancelled));
  read_done = {}; disconnected = {};
}
tl::expected<void, std::error_code> UsbBulkDevice::async_write_borrowed(
    std::span<const uint8_t> data, WriteCallback done) {
  assert(!write_done);
  if (inline_write) done({}, data.size());
  else { leased = data; write_done = std::move(done); }
  return {};
}

int main() {
  using namespace wirelink::astrial;
  auto *port = packet_usb_test_storage();
  PacketUsbConfig config; config.maximum_frame_size = 1036;
  wl_packet_token_t old_token{};
  for (uint64_t generation = 1; generation <= 3; ++generation) {
    PacketUsbAdapter adapter(*port);
    assert(packet_usb_test_init(generation, PacketUsbAdapter::submit,
        PacketUsbAdapter::notify, &adapter) == WL_OK);
    auto bad = config; bad.maximum_frame_size = 2048;
    assert(adapter.start(bad) == WL_ERR_INVALID_ARG);
    assert(adapter.start(config) == WL_OK);
    assert(adapter.start(config) == WL_ERR_INVALID_STATE);
    assert(active.bytes.size() == 2048);
    if (generation == 1) {
      // Empty completion, latched notify-before-query and query-before-wait.
      receive(0);
      while (adapter.wait_for_activity(std::chrono::nanoseconds(0))) {}
      assert(!adapter.ready());
      wl_packet_activity(port);
      assert(adapter.wait_for_activity(std::chrono::milliseconds(1)));
      assert(!adapter.wait_for_activity(std::chrono::nanoseconds(0)));
      // Two queued buffers, a retained borrow, and rearming only on release.
      for (unsigned round = 0; round < 20; ++round) {
        auto previous = active;
        std::memset(active.bytes.data(), 42, 20); receive(20);
        read_done({}, previous, 20); // Old RX cannot abort the next claim.
        std::memset(active.bytes.data(), 43, 21); receive(21);
        assert(active.bytes.empty() && !adapter.ready());
        wl_packet_borrow_t borrow{};
        assert(wl_packet_rx_borrow(port, &borrow) == WL_OK && borrow.data[0] == 42);
        assert(!adapter.ready());
        assert(wl_packet_rx_release(port, &borrow) == WL_OK && adapter.ready());
        assert(adapter.service() == WL_OK && active.bytes.size() == 2048);
        assert(wl_packet_rx_borrow(port, &borrow) == WL_OK && borrow.data[0] == 43);
        assert(wl_packet_rx_release(port, &borrow) == WL_OK);
      }
      uint8_t source[] = {1, 2, 3}; wl_packet_token_t token{};
      assert(wl_packet_tx_submit(port, source, sizeof(source), &token) == WL_OK);
      old_token = token;
      assert(leased.data() == source && !adapter.stopped());
      auto previous = write_done;
      transmit();
      assert(wl_packet_tx_take(port, &token) == (WL_PACKET_CONSUMED | WL_PACKET_DRAINED));
      assert(wl_packet_tx_submit(port, source, sizeof(source), &token) == WL_OK);
      previous({}, sizeof(source)); // Cannot finish or release the new lease.
      assert(!wl_packet_tx_ready(port) && wl_packet_tx_active(port));
      transmit(); (void)wl_packet_tx_take(port, &token);
      inline_write = true;
      assert(wl_packet_tx_submit(port, source, sizeof(source), &token) == WL_OK);
      assert(wl_packet_tx_take(port, &token) == (WL_PACKET_CONSUMED | WL_PACKET_DRAINED));
      inline_write = false;
      // stop must synchronously acknowledge the still leased TX and RX.
      assert(wl_packet_tx_submit(port, source, sizeof(source), &token) == WL_OK);
      adapter.stop();
      assert(!write_done && !provider && !read_done && leased.empty());
      assert(wl_packet_tx_take(port, &token) & WL_PACKET_TX_ERROR);
      assert(adapter.stats().errors == 0 && adapter.stats().stale_completions == 21);
    } else {
      assert(wl_packet_tx_publish(port, old_token, WL_PACKET_DRAINED) == WL_ERR_NOT_FOUND);
      if (generation == 2) receive(2048);
      else disconnected(make_error_code(UsbError::DeviceDisconnected));
      assert(adapter.ready() && adapter.fault() != PacketUsbFault::none);
      unsigned before = resumes;
      for (unsigned i = 0; i < 20; ++i) {
        assert(adapter.service() == WL_ERR_IO && !adapter.ready());
      }
      assert(before == resumes && adapter.stats().errors == 1);
      adapter.stop();
    }
    assert(adapter.stopped() && wl_packet_quiescent(port));
    wl_packet_claim_t claim{};
    assert(wl_packet_rx_claim(port, 1, &claim) == WL_ERR_INVALID_STATE);
  }
}
