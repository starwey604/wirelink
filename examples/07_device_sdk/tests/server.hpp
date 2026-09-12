/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <wirelink/asio/udp_adapter.hpp>
#include <wirelink/platform.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <new>
#include "server_bridge.h"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (0)

// Raw C endpoint with an independent owner loop and C business handlers.
class Server {
public:
  Server() {
    CHECK(device_test_server_init(storage_.get(), &driver_) == WL_OK);
    wirelink::asio::UdpAdapterConfig udp;
    udp.bind_address = "127.0.0.1";
    udp.learn_peer_from_first_datagram = true;
    std::error_code error;
    adapter_ = wirelink::asio::UdpAdapter::open(*driver_.endpoint, udp, error);
    CHECK(adapter_ && !error);
    port_ = adapter_->local_port();
    owner_ = std::thread([this] {
      while (!stopping_) {
        CHECK(driver_.step(driver_.context) == WL_OK);
        wl_poll_hint_t hint;
        CHECK(wl_endpoint_get_hint(driver_.endpoint, &hint) == WL_OK);
        if (!hint.work_pending) {
          const int status = adapter_->wait_for_activity(
              std::chrono::milliseconds(std::min(hint.next_deadline_ms, 100U)));
          CHECK(status == WL_OK || status == WL_ERR_NO_DATA);
        }
      }
      CHECK(driver_.close(driver_.context) == WL_OK);
    });
  }
  ~Server() {
    stopping_ = true;
    adapter_->notify();
    owner_.join();
  }
  std::uint16_t port() const { return port_; }
private:
  struct Deallocate {
    void operator()(void* storage) const noexcept {
      ::operator delete(storage, std::align_val_t(device_test_server_alignment()));
    }
  };
  std::unique_ptr<void, Deallocate> storage_{::operator new(device_test_server_size(),
      std::align_val_t(device_test_server_alignment()))};
  wl_endpoint_driver_t driver_{};
  std::unique_ptr<wirelink::asio::UdpAdapter> adapter_;
  std::uint16_t port_{};
  std::atomic<bool> stopping_{};
  std::thread owner_;
};
