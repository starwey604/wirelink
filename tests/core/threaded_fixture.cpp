/* SPDX-License-Identifier: Apache-2.0 */
#include "threaded_fixture.h"
#include <thread>
int main() {
  wl_test_thread_setup();
  std::thread producer(wl_test_thread_produce);
  wl_test_thread_consume();
  producer.join();
  wl_test_thread_finish();
}
