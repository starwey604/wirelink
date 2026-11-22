/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "service.h"
#include <wirelink/udp.h>
#include <wirelink/host/executor.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::abort(); } } while(0)

class Server {
public:
  Server() {
    wl_udp_config_t config{};
    config.bind_address = "127.0.0.1";
    config.learn_peer = true;
    CHECK(wl_udp_open(&udp_, &config, nullptr) == WL_OK);
    port_ = wl_udp_local_port(udp_);
    device_peer_handlers_t handlers{};
    handlers.on_get_info = [](void *user, const peer_info_request_t *request, peer_info_response_t *response) {
      auto &self = *static_cast<Server *>(user);
      self.before_request();
      return device_test_get_info(&self.state_, request, response);
    };
    handlers.on_configure = [](void *user, const peer_configure_request_t *request, peer_configure_response_t *response) {
      auto &self = *static_cast<Server *>(user);
      self.before_request();
      return device_test_configure(&self.state_, request, response);
    };
    CHECK(device_peer_bind(&peer_, wl_udp_transport(udp_), &handlers, this) == WL_OK);
    CHECK(owner_.initialize(peer_.node) == WL_OK && owner_.start() == WL_OK);
  }
  ~Server() {
    unblock();
    CHECK(owner_.stop() == WL_OK);
    wl_udp_close(udp_);
  }
  uint16_t port()const{return port_;}
  void block(){std::lock_guard lock(gate_);blocked_=true;entered_=false;}
  void unblock(){std::lock_guard lock(gate_);blocked_=false;changed_.notify_all();}
  void wait_entered(){std::unique_lock lock(gate_);CHECK(changed_.wait_for(lock,std::chrono::seconds(5),[&]{return entered_;}));}
private:
  void before_request() {
    std::unique_lock lock(gate_);
    entered_ = true;
    changed_.notify_all();
    changed_.wait(lock, [&] { return !blocked_; });
  }
  device_peer_node_t peer_{};
  device_test_state_t state_{};
  wl_udp_t *udp_{};
  wirelink::host::Executor owner_;
  uint16_t port_{};
  std::mutex gate_;
  std::condition_variable changed_;
  bool blocked_{}, entered_{};
};
