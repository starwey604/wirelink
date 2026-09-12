/* SPDX-License-Identifier: Apache-2.0 */
#include <device/client.hpp>
#include <calculator/client.hpp>
#include <type_traits>
static_assert(!std::is_copy_constructible_v<device::Client>);
static_assert(std::is_nothrow_move_constructible_v<device::Client>);
static_assert(std::is_copy_constructible_v<wirelink::Operation<device::ConfigureResponse>>);
static_assert(std::is_same_v<decltype(std::declval<device::Client&>().configure_async({})),
    wirelink::Result<wirelink::Operation<device::ConfigureResponse>>>);
int main() {
  const auto device = device::Client::connect({"127.0.0.1", 0});
  const auto calculator = calculator::Client::connect({"127.0.0.1", 0});
  if (device || calculator) return 1;
  return device.error().kind == wirelink::ErrorKind::invalid_argument &&
         calculator.error().kind == wirelink::ErrorKind::invalid_argument ? 0 : 2;
}
