/* SPDX-License-Identifier: Apache-2.0 */
#include <calculator/client.hpp>
#include <type_traits>

int main() {
  static_assert(std::is_move_constructible_v<calculator::Client>);
  static_assert(!std::is_copy_constructible_v<calculator::Client>);
  // Deliberate invalid configuration exercises the installed library without IO.
  auto result = calculator::Client::connect({.peer_port = 0});
  return !result && result.error().kind == wirelink::ErrorKind::invalid_argument ? 0 : 1;
}
