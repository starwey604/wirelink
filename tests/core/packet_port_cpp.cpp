/* SPDX-License-Identifier: Apache-2.0 */
#include "packet_port_abi.h"
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <type_traits>

static void check(bool value) { if (!value) std::abort(); }
static_assert(std::is_standard_layout_v<wl_packet_port_t>);

struct Owner {
  wl_packet_port_t port;
  uint8_t rx[WL_PACKET_RX_SLOTS][sizeof(uint32_t)];
  std::atomic<uint32_t> submitted{0};
};

static wl_packet_submit_t submit(void *user, const uint8_t *data, size_t length,
                                  wl_packet_token_t token) {
  auto *owner = static_cast<Owner *>(user);
  check(data != nullptr && length == sizeof(uint32_t));
  owner->submitted.store(token.sequence, std::memory_order_release);
  return WL_PACKET_STARTED;
}

static void exercise(Owner &owner, uint32_t instance) {
  wl_packet_config_t config{};
  config.instance = instance;
  config.rx_storage = &owner.rx[0][0];
  config.rx_unit_size = sizeof(owner.rx[0]);
  config.rx_storage_size = sizeof(owner.rx);
  config.submit = submit;
  config.user = &owner;
  owner.submitted.store(0);
  check(wl_packet_init(&owner.port, &config) == WL_OK);
  constexpr uint32_t count = 10000;
  std::thread producer([&] {
    for (uint32_t value = 1; value <= count; ++value) {
      wl_err_t status;
      do {
        status = wl_packet_rx_push(&owner.port,
            reinterpret_cast<const uint8_t *>(&value), sizeof(value));
      } while (status == WL_ERR_WOULD_BLOCK);
      check(status == WL_OK);
      while (owner.submitted.load(std::memory_order_acquire) < value)
        std::this_thread::yield();
      wl_packet_token_t token{instance, value};
      check(wl_packet_tx_publish(&owner.port, token, WL_PACKET_DRAINED) == WL_OK);
    }
  });
  for (uint32_t value = 1; value <= count; ++value) {
    wl_packet_borrow_t borrow{};
    wl_err_t status;
    do { status = wl_packet_rx_borrow(&owner.port, &borrow); }
    while (status == WL_ERR_NO_DATA);
    check(status == WL_OK && borrow.length == sizeof(value));
    uint32_t received;
    std::memcpy(&received, borrow.data, sizeof(received));
    check(received == value);
    wl_packet_token_t token{};
    check(wl_packet_tx_submit(&owner.port, borrow.data, borrow.length, &token) == WL_OK);
    unsigned events = 0;
    while (!(events & WL_PACKET_DRAINED)) events |= wl_packet_tx_take(&owner.port, &token);
    check((events & WL_PACKET_CONSUMED) != 0);
    check(wl_packet_rx_release(&owner.port, &borrow) == WL_OK);
  }
  producer.join();
  wl_packet_stop(&owner.port);
  check(wl_packet_quiescent(&owner.port));
}

int main() {
#define OFFSET(member) offsetof(wl_packet_port_t, member),
  constexpr size_t layout[] = {
      sizeof(wl_packet_port_t), alignof(wl_packet_port_t),
      WL_TEST_PORT_MEMBERS(OFFSET)
  };
#undef OFFSET
  size_t count = 0;
  const size_t *c_layout = cpp_test_port_layout(&count);
  check(count == sizeof(layout) / sizeof(layout[0]));
  for (size_t i = 0; i < count; ++i) check(layout[i] == c_layout[i]);
  std::array<uint32_t, 2000> identities{};
  check(wl_packet_instance_next(nullptr) == WL_ERR_INVALID_ARG);
  std::thread allocate([&] {
    for (size_t i = 0; i < identities.size() / 2; ++i)
      check(wl_packet_instance_next(&identities[i]) == WL_OK);
  });
  for (size_t i = identities.size() / 2; i < identities.size(); ++i)
    check(wl_packet_instance_next(&identities[i]) == WL_OK);
  allocate.join();
  std::sort(identities.begin(), identities.end());
  check(identities[0] != 0);
  for (size_t i = 1; i < identities.size(); ++i) check(identities[i] > identities[i - 1]);
  Owner owner{};
  exercise(owner, 7701);
  exercise(owner, 7702);
  check(wl_packet_tx_publish(&owner.port, {7701, 1}, WL_PACKET_DRAINED) == WL_ERR_NOT_FOUND);
}
