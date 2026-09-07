/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/host/executor.hpp>

int main() {
  wirelink::host::Executor executor;
  return executor.state() == wirelink::host::Executor::State::kUninitialized ? 0 : 1;
}
