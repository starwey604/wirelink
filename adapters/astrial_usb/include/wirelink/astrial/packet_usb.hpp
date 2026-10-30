/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ASTRIAL_PACKET_USB_HPP_
#define WIRELINK_ASTRIAL_PACKET_USB_HPP_
#include <astrial/Usb.hpp>
#include <chrono>
#include <memory>
#include "wirelink/packet_port.h"

namespace wirelink::astrial {
enum class PacketUsbFault { none, transfer, boundary_lost, invalid_completion, disconnected };
struct PacketUsbConfig {
  UsbBulkConfig usb;
  std::size_t maximum_frame_size{};
  void (*wake)(void *){}; // Nonblocking; may run on I/O thread.
  void *wake_user{};
};
struct PacketUsbStats {
  uint64_t rx_bytes{}, tx_bytes{}, rx_units{}, tx_units{}, errors{}, stale_completions{};
};
// Single owner. Construct before C port init, then start after port init with
// submit/notify below and user=this. Port/RX/outer wake storage must outlive
// stop(). stop() joins the Astrial I/O thread, not merely requests cancellation.
// A fault is sticky: stop, explicitly reset USB framing, recreate with a fresh
// port instance. Reopen alone does not restore the remote packet boundary.
class PacketUsbAdapter {
public:
  explicit PacketUsbAdapter(wl_packet_port_t &);
  ~PacketUsbAdapter();
  PacketUsbAdapter(const PacketUsbAdapter &) = delete;
  PacketUsbAdapter &operator=(const PacketUsbAdapter &) = delete;
  wl_err_t start(const PacketUsbConfig &);
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
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}
#endif
