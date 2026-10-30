/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ASTRIAL_PACKET_SERIAL_HPP_
#define WIRELINK_ASTRIAL_PACKET_SERIAL_HPP_
#include <astrial/Serial.hpp>
#include "wirelink/byte_fifo.h"
#include "wirelink/io_readiness.h"
#include <memory>
namespace wirelink::astrial {
struct PacketSerialConfig {
  std::string port;
  uint32_t baud_rate{115200};
  Parity parity{Parity::None};
  StopBits stop_bits{StopBits::One};
  DataBits data_bits{DataBits::Eight};
  // Required nonblocking owner-side physical boundary query. >0 drained,
  // 0 pending, <0 definite I/O error. Astrial write completion consumes source
  // memory but does not by itself promise hardware line idle.
  int (*drained)(void *){};
  void *drain_user{};
  uint32_t drain_check_ms{1};
};
// C owns port/FIFO atomic storage. Producer only stages bytes; owner service
// runs the selected COBS connector. stop joins the Astrial I/O worker before
// acknowledging cancellation and releasing storage. No automatic reconnect.
class PacketSerialAdapter {
public:
  PacketSerialAdapter(wl_packet_port_t &, wl_byte_fifo_t &, wl_packet_stream_t &);
  ~PacketSerialAdapter();
  PacketSerialAdapter(const PacketSerialAdapter &) = delete;
  PacketSerialAdapter &operator=(const PacketSerialAdapter &) = delete;
  wl_err_t start(const PacketSerialConfig &, std::error_code &);
  wl_err_t service(uint32_t now);
  wl_io_readiness_t readiness(uint32_t now) const;
  bool wait_for_activity(std::chrono::nanoseconds);
  void stop();
  uint64_t errors() const;
  static wl_packet_submit_t submit(void *, const uint8_t *, size_t, wl_packet_token_t);
  static void notify(void *);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace wirelink::astrial
#endif
