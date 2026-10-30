/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ASIO_PACKET_UDP_HPP_
#define WIRELINK_ASIO_PACKET_UDP_HPP_
#include "wirelink/packet_port.h"
#include <chrono>
#include <memory>
#include <string>
#include <system_error>
namespace wirelink::asio {
struct PacketUdpConfig {
  std::string bind_address{"0.0.0.0"};
  uint16_t bind_port{};
  size_t maximum_datagram_size{};
  unsigned service_budget{8};
  bool learn_peer{};
  // Optional selected, in-place transport decoder. Runs only on owner.
  // Updates length to canonical DATA after framing/integrity validation.
  wl_err_t (*decode)(uint8_t *, size_t, size_t *){};
};
struct PacketUdpStats {
  uint64_t received{}, sent{}, rejected{}, errors{};
};
// Owner performs all socket I/O. notify is a latched cross-thread interrupt;
// join its producers before destruction. Caller supplies both RX units.
class PacketUdpAdapter {
public:
  explicit PacketUdpAdapter(wl_packet_port_t &);
  ~PacketUdpAdapter();
  PacketUdpAdapter(const PacketUdpAdapter &) = delete;
  PacketUdpAdapter &operator=(const PacketUdpAdapter &) = delete;
  wl_err_t start(const PacketUdpConfig &, std::error_code &);
  wl_err_t set_peer(const std::string &, uint16_t);
  wl_err_t service();
  bool ready() const;
  wl_err_t wait_for_activity(std::chrono::milliseconds);
  void stop();
  uint16_t local_port() const;
  PacketUdpStats stats() const;
  static wl_packet_submit_t submit(void *, const uint8_t *, size_t, wl_packet_token_t);
  static void notify(void *);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace wirelink::asio
#endif
