/* SPDX-License-Identifier: Apache-2.0 */
#include "bridge.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main() {
  using Owner = std::unique_ptr<void, decltype(&storage_bridge_destroy)>;
  CHECK(storage_bridge_create(1) == nullptr); // Roll back first endpoint.
  int32_t saved{};
  for (unsigned pass = 0; pass < 20; ++pass) {
    Owner owner(storage_bridge_create(0), storage_bridge_destroy);
    CHECK(owner);
    for (int32_t i = 0; i < 100; ++i) {
      CHECK(storage_bridge_call(owner.get(), i, &saved) == 0 && saved == i);
      CHECK(storage_bridge_allocations(owner.get()) == 2);
      CHECK(storage_bridge_deallocations(owner.get()) == 0);
    }
    CHECK(storage_bridge_close(owner.get()) == 0);
    CHECK(storage_bridge_close(owner.get()) == 0);
    CHECK(storage_bridge_deallocations(owner.get()) == 2);
    CHECK(storage_bridge_call(owner.get(), 1, &saved) == -1 && saved == 99);
  }
  CHECK(saved == 99);
  std::puts("C++ RAII -> C -> allocated endpoints: rollback/pairing/2000 sync/zero hot allocations PASS");
}
