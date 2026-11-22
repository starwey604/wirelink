/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_PACKET_TOOL_IO_HPP
#define WIRELINK_EXAMPLE_PACKET_TOOL_IO_HPP
#include "packet_tool.h"
#include "wirelink/astrial/packet_serial.hpp"
#include "wirelink/astrial/packet_usb.hpp"
#include "wirelink/platform.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#ifdef __linux__
#include <fcntl.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
// Private tool assembly: single owner, one active C fixture per process.
class ExamplePacketIo {
public:
  using Clock = std::chrono::steady_clock;
  ExamplePacketIo() = default;
  ExamplePacketIo(const ExamplePacketIo &) = delete;
  ExamplePacketIo &operator=(const ExamplePacketIo &) = delete;
  ~ExamplePacketIo() { close(); }
  void usb(uint16_t vid, uint16_t pid, const std::string &serial = {}) {
    if (owned_) throw std::runtime_error("tool already initialized");
    wirelink::astrial::PacketUsbConfig config;
    config.usb.device.vendor_id = vid; config.usb.device.product_id = pid;
    config.usb.device.serial_number = serial;
    config.rx = {&usb_rx_[0][0], sizeof(usb_rx_), sizeof(usb_rx_[0])};
    usb_ = std::make_unique<wirelink::astrial::PacketUsbAdapter>(config);
    initialize(false, usb_->transport());
  }
  void serial(const std::string &port, uint32_t baud = 3000000) {
    if (owned_) throw std::runtime_error("tool already initialized");
#ifdef __linux__
    // A queue-empty query does not establish physical line idle. Refuse drivers
    // without the hardware transmitter-empty query, before starting any I/O.
    fd_ = ::open(port.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) throw std::runtime_error("cannot open serial drain-query descriptor");
    if (drained(this) < 0) throw std::runtime_error("serial physical drain query unsupported (TIOCSERGETLSR)");
#else
    (void)port; (void)baud;
    throw std::runtime_error("this example requires a platform physical drain query");
#endif
    wirelink::astrial::PacketSerialConfig config;
    config.port = port; config.baud_rate = baud; config.drained = drained; config.drain_user = this;
    serial_ = std::make_unique<wirelink::astrial::PacketSerialAdapter>(
        config, wl_span_t{fifo_, sizeof(fifo_)});
    initialize(true, serial_->transport());
  }
  static uint32_t now() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now().time_since_epoch()).count());
  }
  void service() {
    check(example_packet_tool_step(now()));
  }
  void wait(Clock::time_point deadline) {
    const auto left = deadline - Clock::now();
    if (left <= Clock::duration::zero()) return;
    const auto ms = std::chrono::ceil<std::chrono::milliseconds>(left).count();
    check(wl_node_wait(example_packet_tool_node(), now(),
                       static_cast<uint32_t>(std::min<int64_t>(ms, UINT32_MAX))));
  }
  static void check(wl_err_t error) {
    if (error != WL_OK && error != WL_ERR_NO_DATA && error != WL_ERR_WOULD_BLOCK)
      throw std::runtime_error("packet error " + std::to_string(error));
  }
  void close() noexcept {
    if (owned_ && example_packet_tool_close(now()) != WL_OK) std::terminate();
    usb_.reset(); serial_.reset(); owned_ = false;
#ifdef __linux__
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
  }
private:
  void initialize(bool serial, wl_transport_t *transport) {
    uint32_t instance = 0;
    check(wl_session_next(wl_platform_environment().session, 0, &instance));
    check(example_packet_tool_bind(instance, serial, transport));
    owned_ = true;
  }
  static int drained(void *context) {
#ifdef __linux__
    int state = 0;
    if (::ioctl(static_cast<ExamplePacketIo *>(context)->fd_, TIOCSERGETLSR, &state) < 0) return -1;
    return (state & TIOCSER_TEMT) != 0;
#else
    (void)context; return -1;
#endif
  }
  bool owned_{};
  int fd_{-1};
  uint8_t fifo_[1024]{};
  alignas(512) uint8_t usb_rx_[WL_PACKET_RX_SLOTS][2560]{};
  std::unique_ptr<wirelink::astrial::PacketUsbAdapter> usb_;
  std::unique_ptr<wirelink::astrial::PacketSerialAdapter> serial_;
};
#endif
