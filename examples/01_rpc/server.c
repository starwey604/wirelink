/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_endpoint.h"
#include "tutorial_host.h"

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  const int64_t sum = (int64_t)request->left + request->right;
  (void)context;
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  if (sum < INT32_MIN || sum > INT32_MAX)
    return 1; /* Business rejection; framework errors are separate. */
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(calculator_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);

  config.on_add = add;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&server) == WL_OK);
  example_udp_close(udp);
  return 0;
}
