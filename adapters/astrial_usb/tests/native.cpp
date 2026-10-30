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
void UsbBulkDevice::close() {
  provider = {};
  if (!active.bytes.empty()) complete(0, make_error_code(UsbError::TransferCancelled));
  callback = {};
}
tl::expected<void, std::error_code> UsbBulkDevice::async_write_borrowed(
    std::span<const uint8_t> data, WriteCallback done) {
  done({}, data.size()); return {};
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string_view mode(argv[1]);
  wl_ctx_t link{};
  uint8_t payload[1032], unit[1052], control[20], fallback[1052];
  wl_config_t config{};
  config.max_payload_len = sizeof(payload); config.envelope = WL_ENVELOPE_NATIVE_PACKET;
  config.integrity = WL_INTEGRITY_NONE; config.session_id = 1;
  wl_storage_t storage{};
  storage.tx_payload = payload; storage.tx_payload_size = sizeof(payload);
  storage.tx_unit = unit; storage.tx_unit_size = sizeof(unit);
  storage.control_unit = control; storage.control_unit_size = sizeof(control);
  storage.rx_fallback = fallback; storage.rx_fallback_size = sizeof(fallback);
  assert(wl_init(&link, &config, &storage) == WL_OK);
  wirelink::astrial::UsbBulkAdapterConfig adapter_config;
  adapter_config.maximum_read_size = mode == "capacity" ? 1048 : 2048;
  adapter_config.unit_queue_slots = 2;
  auto opened = wirelink::astrial::UsbBulkAdapter::open(link, adapter_config);
  if (mode == "capacity") { assert(!opened); return 0; }
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
    const unsigned before = resumes;
    for (int i = 0; i < 20; ++i) {
      assert(adapter->service() == WL_OK);
      assert(!adapter->readiness().runnable);
    }
    assert(resumes == before && active.bytes.empty());
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
