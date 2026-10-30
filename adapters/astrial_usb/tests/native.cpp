/* SPDX-License-Identifier: Apache-2.0 */
/* The production adapter, core and public Astrial API with a controlled USB
 * completion source. No USB controller or ZLP behavior is simulated here. */
#include <wirelink/astrial/usb_bulk_adapter.hpp>
#include <wirelink/frame.h>
#include <cassert>
#include <cstring>
#include <string_view>
#include <utility>

namespace {
UsbBulkDevice::ReadBufferProvider provider;
UsbBulkDevice::ReadCallback callback;
UsbBorrowedBuffer active;
unsigned resumes;
std::function<void(const std::error_code&)> disconnected;
void arm() { if (provider && active.bytes.empty()) active = provider(); }
void complete(std::size_t length, std::error_code error = {}) {
  auto buffer = std::exchange(active, {});
  assert(!buffer.bytes.empty());
  callback(error, buffer, length);
  arm();
}
}

class UsbBulkDevice::Impl {};
UsbBulkDevice::UsbBulkDevice(std::unique_ptr<Impl> value) : m_impl(std::move(value)) {}
UsbBulkDevice::~UsbBulkDevice() = default;
UsbBulkDevice::UsbBulkDevice(UsbBulkDevice&&) noexcept = default;
UsbBulkDevice& UsbBulkDevice::operator=(UsbBulkDevice&&) noexcept = default;
std::error_code make_error_code(UsbError error) { return {int(error), std::generic_category()}; }
tl::expected<UsbBulkDevice, std::error_code> UsbBulkDevice::open(const UsbBulkConfig& config) {
  assert(config.read_queue_depth == 1);
  return UsbBulkDevice(std::make_unique<Impl>());
}
UsbBulkEndpointInfo UsbBulkDevice::endpoint_info() const noexcept { return {64, 64}; }
tl::expected<void, std::error_code> UsbBulkDevice::start_reads_borrowed(
    ReadBufferProvider next, ReadCallback done) {
  provider = std::move(next); callback = std::move(done); arm(); return {};
}
void UsbBulkDevice::resume_reads() { ++resumes; arm(); }
void UsbBulkDevice::on_disconnect(std::function<void(const std::error_code&)> cb) { disconnected = std::move(cb); }
void UsbBulkDevice::close() {
  provider = {};
  if (!active.bytes.empty()) complete(0, make_error_code(UsbError::TransferCancelled));
  callback = {}; disconnected = {};
}
tl::expected<void, std::error_code> UsbBulkDevice::async_write_borrowed(
    std::span<const uint8_t> data, WriteCallback done) {
  done({}, data.size()); return {};
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string_view mode(argv[1]);
  wl_ctx_t link{};
  uint8_t payload[1032], unit[1100], control[20], fallback[1100];
  wl_config_t config{};
  config.max_payload_len = sizeof(payload); config.envelope = WL_ENVELOPE_NATIVE_PACKET;
  config.integrity = WL_INTEGRITY_NONE; config.session_id = 1;
  wl_storage_t storage{};
  storage.tx_payload = payload; storage.tx_payload_size = sizeof(payload);
  storage.tx_unit = unit; storage.tx_unit_size = sizeof(unit);
  storage.control_unit = control; storage.control_unit_size = sizeof(control);
  storage.rx_fallback = fallback; storage.rx_fallback_size = sizeof(fallback);
  const auto usb_config = wl_usb_bulk_native_packet(2048, 2);
  if (mode != "cobs") assert(wl_usb_bulk_config_apply(&config, usb_config) == WL_OK);
  uint8_t fifo[4096];
  if (mode == "cobs") {config.envelope = WL_ENVELOPE_COBS_STREAM; storage.rx_fifo=fifo; storage.rx_fifo_size=sizeof(fifo);}
  assert(wl_init(&link, &config, &storage) == WL_OK);
  uint8_t rx_storage[2][2048];
  if (mode != "unprepared" && mode != "cobs")
    assert(wl_usb_bulk_rx_init(&link, usb_config, &rx_storage[0][0], sizeof(rx_storage)) == WL_OK);
  wirelink::astrial::UsbBulkAdapterConfig adapter_config;
  adapter_config.transport = mode == "capacity" ? wl_usb_bulk_native_packet(1048, 2) : usb_config;
  if (mode == "mismatch") adapter_config.transport.integrity = WL_INTEGRITY_CRC16;
  wl_event_t retained{};
  if (mode == "prequeued" || mode == "leased") {
    wl_rx_unit_claim_t claim{}; std::size_t length{};
    const uint8_t data[] = {42};
    wl_wire_packet_t packet{};
    packet.type = WL_PACKET_DATA; packet.integrity = WL_INTEGRITY_NONE;
    packet.message_id = 103; packet.payload = data; packet.payload_len = 1;
    assert(wl_rx_unit_claim(&link, 2048, &claim) == WL_OK);
    assert(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
        claim.span.data, claim.span.length, &length) == WL_OK);
    assert(wl_rx_unit_commit(&link, &claim, length) == WL_OK);
    if (mode == "leased") assert(wl_poll(&link, 0, &retained) == WL_OK);
  }
  auto opened = wirelink::astrial::UsbBulkAdapter::open(link, adapter_config);
  if (mode == "capacity" || mode == "unprepared" || mode == "mismatch" || mode == "cobs") {
    assert(!opened);
    wl_config_t actual{}; assert(wl_get_config(&link, &actual) == WL_OK);
    assert(actual.envelope == config.envelope && actual.integrity == config.integrity);
    return 0;
  }
  assert(opened);
  auto adapter = std::move(*opened);
  assert(active.bytes.size() == 2048);
  if (mode == "empty") {
    complete(0);
    assert(adapter->wait_for_activity(std::chrono::nanoseconds(0)));
    assert(!adapter->wait_for_activity(std::chrono::nanoseconds(0)));
    wl_adapter_stats_t stats{}; adapter->get_common_stats(stats);
    assert(stats.errors == 0 && stats.rx_bytes == 0);
    wl_event_t event{}; assert(wl_poll(&link, 0, &event) == WL_ERR_NO_DATA);
    assert(active.bytes.size() == 2048);
  } else if (mode == "full" || mode == "error") {
    complete(mode == "full" ? 2048 : 12,
             mode == "full" ? std::error_code{} : make_error_code(UsbError::TransferTimeout));
    wl_adapter_stats_t stats{}; adapter->get_common_stats(stats);
    assert(stats.errors > 0 && stats.rx_bytes == 0);
    assert(active.bytes.empty());
    assert(adapter->readiness().runnable);
    assert(adapter->rx_fault() == (mode == "full" ? WL_USB_BULK_RX_FAULT_BOUNDARY_LOST : WL_USB_BULK_RX_FAULT_TRANSFER));
    const unsigned before = resumes;
    for (int i = 0; i < 20; ++i) {
      assert(adapter->service() == WL_ERR_IO);
      assert(!adapter->readiness().runnable);
    }
    assert(resumes == before && active.bytes.empty());
  } else if (mode == "prequeued" || mode == "leased") {
    if (mode == "prequeued") assert(wl_poll(&link, 0, &retained) == WL_OK);
    assert(retained.payload_len == 1 && retained.payload[0] == 42);
    wl_event_release(&link, &retained);
  } else if (mode == "disconnect") {
    assert(disconnected);
    disconnected(make_error_code(UsbError::DeviceDisconnected));
    assert(adapter->readiness().runnable);
    assert(adapter->rx_fault() == WL_USB_BULK_RX_FAULT_DISCONNECTED);
    assert(adapter->service() == WL_ERR_IO);
    assert(!adapter->readiness().runnable);
  } else if (mode == "queue") {
    const uint8_t bytes[] = {1, 2, 3};
    wl_wire_packet_t packet{};
    packet.type = WL_PACKET_DATA; packet.integrity = WL_INTEGRITY_NONE;
    packet.message_id = 103; packet.payload = bytes; packet.payload_len = sizeof(bytes);
    for (int round = 0; round < 20; ++round) {
      for (int slot = 0; slot < 2; ++slot) {
        std::size_t length{};
        assert(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
            active.bytes.data(), active.bytes.size(), &length) == WL_OK);
        complete(length);
      }
      assert(active.bytes.empty());
      wl_event_t event{};
      assert(wl_poll(&link, 0, &event) == WL_OK);
      assert(event.type == WL_EVT_UNRELIABLE_RX && event.payload_len == sizeof(bytes));
      assert(std::memcmp(event.payload, bytes, sizeof(bytes)) == 0);
      assert(!adapter->readiness().runnable);
      wl_event_release(&link, &event);
      assert(adapter->readiness().runnable);
      assert(adapter->service() == WL_OK && active.bytes.size() == 2048);
      assert(wl_poll(&link, 0, &event) == WL_OK);
      assert(std::memcmp(event.payload, bytes, sizeof(bytes)) == 0);
      wl_event_release(&link, &event);
    }
  } else { assert(false); }
  wl_adapter_stats_t before{}, after{};
  adapter->get_common_stats(before);
  adapter->quiesce(); adapter->get_common_stats(after);
  assert(after.errors == before.errors); // Expected close cancellation is not an IO failure.
}
