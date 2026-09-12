/* SPDX-License-Identifier: Apache-2.0 */
#include "server.hpp"
#include <csignal>

static volatile std::sig_atomic_t running = 1;
static void stop(int) { running = 0; }
int main() {
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  Server server;
  std::printf("%u\n", static_cast<unsigned>(server.port()));
  std::fflush(stdout);
  while (running) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
