/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/detail/coalescing_event.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using wirelink::detail::CoalescingEvent;
using namespace std::chrono_literals;

static void require(bool value) {
  if (!value) std::abort();
}

int main() {
  CoalescingEvent event;
  require(!event.wait_for(1ms));
  for (unsigned i = 0; i < 1000000; ++i) event.notify();
  require(event.wait_for(1s));
  require(!event.wait_for(1ms)); // One pending wake, not a million stale permits.
  event.notify();
  event.wait();

  // Ordinary data is published by notify/wait; no atomics on the payload.
  CoalescingEvent ack;
  unsigned payload{};
  std::thread writer([&] {
    for (unsigned i = 1; i <= 20000; ++i) {
      payload = i;
      event.notify();
      ack.wait();
    }
  });
  for (unsigned i = 1; i <= 20000; ++i) {
    require(event.wait_for(5s));
    require(payload == i);
    ack.notify();
  }
  writer.join();

  // MPSC notification storms race with the consumer clearing the pending bit.
  // The last publication must not be lost when producers all become idle.
  std::atomic<unsigned> finished{};
  std::array<std::thread, 4> producers;
  for (auto& producer : producers) producer = std::thread([&] {
    for (unsigned i = 0; i < 100000; ++i) event.notify();
    finished.fetch_add(1, std::memory_order_release);
    event.notify();
  });
  while (finished.load(std::memory_order_acquire) != producers.size())
    require(event.wait_for(5s));
  for (auto& producer : producers) producer.join();
  std::puts("PASS: bounded notification, publication, and MPSC idle transition");
}
