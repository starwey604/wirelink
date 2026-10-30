/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "server_bridge.h"
#include <wirelink/asio/packet_udp.hpp>
#include <wirelink/platform.h>
#include <wirelink/host/clock.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::abort(); } } while(0)
// C owns endpoint/atomic storage; this independent application loop owns I/O.
class Server {
public:
  Server() {
    CHECK(calculator_test_server_prepare(storage_.get(),&descriptor_)==WL_OK);
    adapter_=std::make_unique<wirelink::asio::PacketUdpAdapter>(*descriptor_.port);
    auto environment=wl_platform_environment(); uint64_t instance;
    CHECK(wl_session_next(environment.session,0,&instance)==WL_OK);
    CHECK(calculator_test_server_initialize(storage_.get(),instance,
      wirelink::asio::PacketUdpAdapter::submit,wirelink::asio::PacketUdpAdapter::notify,adapter_.get())==WL_OK);
    calculator_test_server_hook(storage_.get(),[](void* user) {
      auto& self=*static_cast<Server*>(user);std::unique_lock lock(self.gate_);
      self.entered_=true;self.changed_.notify_all();self.changed_.wait(lock,[&] {return !self.blocked_;});
    },this);
    wirelink::asio::PacketUdpConfig config;
    config.bind_address="127.0.0.1";config.maximum_datagram_size=descriptor_.maximum_rx_frame;config.learn_peer=true;
    std::error_code error;CHECK(adapter_->start(config,error)==WL_OK && !error);
    port_=adapter_->local_port();
    owner_=std::thread([this] {
      const auto clock=wirelink::host::monotonic_clock();
      while(!stopping_) {
        CHECK(adapter_->service()==WL_OK);
        const uint32_t now=clock.now_ms(clock.user_data);
        CHECK(wl_kernel_step(descriptor_.core,now,16)==WL_OK);
        int has_deadline=0;uint32_t deadline=0;
        if(!wl_kernel_ready(descriptor_.core,now,&has_deadline,&deadline)) {
          const auto remaining=has_deadline ? deadline-now : UINT32_MAX;
          const auto result=adapter_->wait_for_activity(std::chrono::milliseconds(std::min(remaining,100U)));
          CHECK(result==WL_OK || result==WL_ERR_NO_DATA);
        }
      }
      CHECK(wl_kernel_close(descriptor_.core)==WL_OK);
      adapter_->stop();
      CHECK(wl_kernel_step(descriptor_.core,clock.now_ms(clock.user_data),16)==WL_OK);
      CHECK(wl_kernel_quiescent(descriptor_.core));
    });
  }
  ~Server() {unblock();stopping_=true;wirelink::asio::PacketUdpAdapter::notify(adapter_.get());owner_.join();}
  uint16_t port()const{return port_;}
  void block(){std::lock_guard lock(gate_);blocked_=true;entered_=false;}
  void unblock(){std::lock_guard lock(gate_);blocked_=false;changed_.notify_all();}
  void wait_entered(){std::unique_lock lock(gate_);CHECK(changed_.wait_for(lock,std::chrono::seconds(5),[&]{return entered_;}));}
private:
  struct Delete { void operator()(void* pointer)const noexcept {
    ::operator delete(pointer,std::align_val_t(calculator_test_server_alignment()));
  }};
  std::unique_ptr<void,Delete> storage_{::operator new(calculator_test_server_size(),std::align_val_t(calculator_test_server_alignment()))};
  wl_host_endpoint_t descriptor_{};
  std::unique_ptr<wirelink::asio::PacketUdpAdapter> adapter_;
  uint16_t port_{};std::atomic<bool> stopping_{};std::thread owner_;
  std::mutex gate_;std::condition_variable changed_;bool blocked_{},entered_{};
};
