/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_PACKET_TOOL_IO_HPP
#define WIRELINK_EXAMPLE_PACKET_TOOL_IO_HPP
#include "packet_tool_owner.h"
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
    initialize(false);
    usb_ = std::make_unique<wirelink::astrial::PacketUsbAdapter>(*example_packet_tool_port());
    wirelink::astrial::PacketUsbConfig config;
    config.usb.device.vendor_id = vid; config.usb.device.product_id = pid;
    config.usb.device.serial_number = serial;
    config.maximum_frame_size = EXAMPLE_PACKET_TOOL_FRAME;
    check(usb_->start(config));
  }
  void serial(const std::string &port, uint32_t baud = 3000000) {
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
    initialize(true);
    serial_ = std::make_unique<wirelink::astrial::PacketSerialAdapter>(
      *example_packet_tool_port(), *example_packet_tool_fifo(), *example_packet_tool_stream());
    wirelink::astrial::PacketSerialConfig config;
    config.port = port; config.baud_rate = baud; config.drained = drained; config.drain_user = this;
    std::error_code error;
    const auto result = serial_->start(config, error);
    if (result != WL_OK) throw std::runtime_error("serial start: " + error.message());
  }
  static uint32_t now() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now().time_since_epoch()).count());
  }
  void service() {
    if (usb_) check(usb_->service());
    if (serial_) check(serial_->service(now()));
    check(example_packet_tool_step(now()));
  }
  void wait(Clock::time_point deadline) {
    const auto tick = now();
    if (example_packet_tool_ready(tick) || (usb_ && usb_->ready())) return;
    auto duration = deadline - Clock::now();
    if (serial_) {
      const auto hint = serial_->readiness(tick);
      if (hint.runnable) return;
      if (hint.next_deadline_ms != UINT32_MAX)
        duration = std::min(duration, std::chrono::duration_cast<Clock::duration>(
          std::chrono::milliseconds(hint.next_deadline_ms)));
    }
    // Rebase after all owner work; adapter waits consume a latched wake.
    duration = std::min(duration, deadline - Clock::now());
    if (duration <= Clock::duration::zero()) return;
    if (usb_) (void)usb_->wait_for_activity(duration);
    if (serial_) (void)serial_->wait_for_activity(duration);
  }
  static void check(wl_err_t error) {
    if (error != WL_OK && error != WL_ERR_NO_DATA && error != WL_ERR_WOULD_BLOCK)
      throw std::runtime_error("packet error " + std::to_string(error));
  }
  void close() noexcept {
    if (owned_) (void)example_packet_tool_close_begin();
    // Actual worker join precedes cancellation processing or storage release.
    if (usb_) usb_->stop();
    if (serial_) serial_->stop();
    if (owned_ && example_packet_tool_close_finish(now()) != WL_OK) std::terminate();
    usb_.reset(); serial_.reset(); owned_ = false;
#ifdef __linux__
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
  }
private:
  void initialize(bool serial) {
    if (owned_) throw std::runtime_error("tool already initialized");
    uint64_t instance = 0;
    check(wl_session_next(wl_platform_environment().session, 0, &instance));
    check(example_packet_tool_prepare(instance, serial, submit, notify, this));
    owned_ = true;
  }
  static wl_packet_submit_t submit(void *context, const uint8_t *data, size_t length,
                                  wl_packet_token_t token) {
    auto &self = *static_cast<ExamplePacketIo *>(context);
    if (self.usb_) return wirelink::astrial::PacketUsbAdapter::submit(self.usb_.get(), data, length, token);
    if (self.serial_) return wirelink::astrial::PacketSerialAdapter::submit(self.serial_.get(), data, length, token);
    return WL_PACKET_FAILED;
  }
  static void notify(void *context) {
    auto &self = *static_cast<ExamplePacketIo *>(context);
    if (self.usb_) wirelink::astrial::PacketUsbAdapter::notify(self.usb_.get());
    if (self.serial_) wirelink::astrial::PacketSerialAdapter::notify(self.serial_.get());
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
  std::unique_ptr<wirelink::astrial::PacketUsbAdapter> usb_;
  std::unique_ptr<wirelink::astrial::PacketSerialAdapter> serial_;
};
#endif
