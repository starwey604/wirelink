/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ASTRIAL_PACKET_USB_HPP_
#define WIRELINK_ASTRIAL_PACKET_USB_HPP_
#include <astrial/Usb.hpp>
#include <chrono>
#include <memory>
#include "wirelink/transport.h"
#include "wirelink/packet_port.h"

namespace wirelink::astrial {
enum class PacketUsbFault { none, transfer, boundary_lost, invalid_completion, disconnected };
struct PacketUsbConfig {
  UsbBulkConfig usb;
  wl_transport_rx_override_t rx{}; // Optional slots, each a multiple of the USB IN packet size.
  void (*wake)(void *){}; // Nonblocking; may run on I/O thread.
  void *wake_user{};
};
struct PacketUsbStats {
  uint64_t rx_bytes{}, tx_bytes{}, rx_units{}, tx_units{}, errors{}, stale_completions{};
};
// Single owner. Node bind opens the device; stop joins borrowed I/O callbacks.
// A fault is sticky: stop, explicitly reset USB framing, recreate with a fresh
// port instance. Reopen alone does not restore the remote packet boundary.
class PacketUsbAdapter : private wl_transport_t {
public:
  explicit PacketUsbAdapter(PacketUsbConfig config);
  ~PacketUsbAdapter();
  PacketUsbAdapter(const PacketUsbAdapter &) = delete;
  PacketUsbAdapter &operator=(const PacketUsbAdapter &) = delete;
  wl_transport_t *transport() noexcept { return this; }
  wl_err_t service();
  bool ready() const;
  void stop();
  bool stopped() const;
  PacketUsbFault fault() const;
  PacketUsbStats stats() const;
  UsbBulkEndpointInfo endpoint_info() const;
  bool wait_for_activity(std::chrono::nanoseconds);
  static wl_packet_submit_t submit(void *, const uint8_t *, size_t, wl_packet_token_t);
  static void notify(void *);
private:
  wl_err_t attach(const wl_transport_attach_t &);
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}
#endif
