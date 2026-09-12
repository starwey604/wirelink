/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CALCULATOR_TEST_SERVER_HPP
#define CALCULATOR_TEST_SERVER_HPP
#include "calculator_endpoint.h"
#include "service.h"
#include <wirelink/asio/udp_adapter.hpp>
#include <wirelink/platform.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (0)

// Independent C endpoint and owner loop: deliberately does not use Session.
class Server {
public:
  Server() {
    calculator_endpoint_config_t config;
    CHECK(calculator_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);
    config.on_add = [](void* context, const add_request_value_t* request, add_response_value_t* response) {
      auto& self = *static_cast<Server*>(context);
      {
        std::unique_lock lock(self.gate_);
        self.entered_ = true;
        self.ready_.notify_all();
        self.ready_.wait(lock, [&self] { return !self.blocked_; });
      }
      return calculator_test_add(nullptr, request, response);
    };
    config.user_data = this;
    CHECK(calculator_endpoint_init_config(&endpoint_, &config) == WL_OK);
    wirelink::asio::UdpAdapterConfig udp;
    udp.bind_address = "127.0.0.1";
    udp.learn_peer_from_first_datagram = true;
    std::error_code error;
    adapter_ = wirelink::asio::UdpAdapter::open(*calculator_endpoint_handle(&endpoint_), udp, error);
    CHECK(adapter_ && !error);
    port_ = adapter_->local_port();
    owner_ = std::thread([this] {
      while (!stopping_) {
        CHECK(calculator_endpoint_step(&endpoint_) == WL_OK);
        wl_poll_hint_t hint;
        CHECK(wl_endpoint_get_hint(calculator_endpoint_handle(&endpoint_), &hint) == WL_OK);
        if (!hint.work_pending) {
          const int status = adapter_->wait_for_activity(
              std::chrono::milliseconds(std::min(hint.next_deadline_ms, 100U)));
          CHECK(status == WL_OK || status == WL_ERR_NO_DATA);
        }
      }
      CHECK(calculator_endpoint_close(&endpoint_) == WL_OK);
    });
  }
  ~Server() {
    unblock();
    stopping_ = true;
    adapter_->notify();
    owner_.join();
  }
  std::uint16_t port() const { return port_; }
  void block() { std::lock_guard lock(gate_); blocked_ = true; entered_ = false; }
  void wait_entered() {
    std::unique_lock lock(gate_);
    CHECK(ready_.wait_for(lock, std::chrono::seconds(5), [this] { return entered_; }));
  }
  void unblock() {
    std::lock_guard lock(gate_);
    blocked_ = false;
    ready_.notify_all();
  }

private:
  calculator_endpoint_t endpoint_{};
  std::unique_ptr<wirelink::asio::UdpAdapter> adapter_;
  std::uint16_t port_{};
  std::mutex gate_;
  std::condition_variable ready_;
  bool blocked_{}, entered_{};
  std::atomic<bool> stopping_{};
  std::thread owner_;
};
#endif
