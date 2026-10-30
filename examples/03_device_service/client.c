/* SPDX-License-Identifier: Apache-2.0 */
#include "device_service.h"
#include "self_test.h"
#include "loopback_owner.h"
void device_example_handlers(device_kernel_handlers_t *, device_service_t *);
static device_kernel_t endpoints[2];
static device_service_t service;
static example_pair_t pair;
static unsigned telemetry_count, job_count;
static uint32_t completed_job, observed;
EXAMPLE_COMPLETION(ping, ping_response)
EXAMPLE_COMPLETION(get_info, get_info_response)
EXAMPLE_COMPLETION(set_name, set_name_response)
EXAMPLE_COMPLETION(read_register, read_register_response)
EXAMPLE_COMPLETION(write_register, write_register_response)
EXAMPLE_COMPLETION(get_counters, get_counters_response)
EXAMPLE_COMPLETION(reset_counters, reset_counters_response)
EXAMPLE_COMPLETION(add, add_response)
EXAMPLE_COMPLETION(scale, scale_response)
EXAMPLE_COMPLETION(get_limits, get_limits_response)
EXAMPLE_COMPLETION(set_limits, set_limits_response)
EXAMPLE_COMPLETION(self_test, self_test_response)
static unsigned starts;
static wl_kernel_result_t start_results[2];
static uint32_t job_id;
static void start_completed(void *user, wl_kernel_result_t result, const start_self_test_response_t *response) {
  const unsigned slot = *(const unsigned *)user;
  EXAMPLE_REQUIRE(slot < 2);
  start_results[slot] = result;
  if (response) job_id = response->job_id;
  ++starts;
}
static void telemetry(void *user, const device_telemetry_t *message) {
  (void)user;
  EXAMPLE_REQUIRE(message->sequence == 42 && message->channels[99] == UINT32_MAX);
  ++telemetry_count;
}
static void progress(void *user, const self_test_progress_t *message) {
  (void)user; completed_job = message->job_id; observed = message->observed; ++job_count;
}
static void advance(void) {
  service.now = pair.now;
  self_test_poll(&service, &endpoints[1]);
  example_pair_step(&pair);
}
static void await(const unsigned *count, unsigned target) {
  for (unsigned pass = 0; pass < 500 && *count < target; ++pass) advance();
  EXAMPLE_REQUIRE(*count == target);
}
#define CALL(method, request) do { \
  wl_kernel_call_handle_t handle; unsigned target = method##_count + 1; \
  EXAMPLE_REQUIRE(device_kernel_##method##_start(&endpoints[0], &(request), pair.now, 500, \
    method##_completed, NULL, &handle) == WL_OK); \
  await(&method##_count, target); \
} while (0)
#define SUCCESS(method) EXAMPLE_REQUIRE(method##_result.outcome == WL_KERNEL_SUCCESS)
/* extension: completions */
int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  device_kernel_handlers_t handlers;
  device_example_handlers(&handlers, &service);
  handlers.on_message_device_telemetry = telemetry;
  handlers.on_message_self_test_progress = progress;
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(device_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, &service) == WL_OK);
  ping_request_value_t ping = {.has_nonce = true, .nonce = UINT32_MAX};
  CALL(ping, ping); SUCCESS(ping); EXAMPLE_REQUIRE(ping_value.nonce == UINT32_MAX);
  get_info_request_value_t info = {0}; CALL(get_info, info); SUCCESS(get_info);
  set_name_request_value_t name = {.has_name = true, .name = {.length = 3, .data = {'a', 0, 'b'}}};
  CALL(set_name, name); SUCCESS(set_name); CALL(get_info, info);
  EXAMPLE_REQUIRE(get_info_value.name.length == 3 && memcmp(get_info_value.name.data, "a\0b", 3) == 0);
  write_register_request_value_t write = {.has_index = true, .index = 4, .has_value = true, .value = 7};
  CALL(write_register, write);
  EXAMPLE_REQUIRE(write_register_result.outcome == WL_KERNEL_REJECTED && write_register_result.status == DEVICE_INVALID_ARGUMENT);
  set_limits_request_value_t limits = {.has_lower = true, .lower = -10, .has_upper = true, .upper = 10};
  CALL(set_limits, limits); SUCCESS(set_limits);
  write.index = 1; write.value = 100; CALL(write_register, write);
  EXAMPLE_REQUIRE(write_register_result.outcome == WL_KERNEL_REJECTED && write_register_result.status == DEVICE_OUT_OF_RANGE);
  write.value = 7; CALL(write_register, write); SUCCESS(write_register);
  read_register_request_value_t read = {.has_index = true, .index = 1};
  CALL(read_register, read); SUCCESS(read_register); EXAMPLE_REQUIRE(read_register_value.value == 7);
  get_counters_request_value_t counters = {0}; CALL(get_counters, counters); SUCCESS(get_counters);
  EXAMPLE_REQUIRE(get_counters_value.writes == 1);
  reset_counters_request_value_t reset = {0}; CALL(reset_counters, reset); SUCCESS(reset_counters);
  CALL(get_counters, counters); EXAMPLE_REQUIRE(get_counters_value.writes == 0);
  get_limits_request_value_t query = {0}; CALL(get_limits, query); SUCCESS(get_limits);
  EXAMPLE_REQUIRE(get_limits_value.lower == -10 && get_limits_value.upper == 10);
  add_request_value_t add = {.has_left = true, .left = 20, .has_right = true, .right = 22};
  CALL(add, add); SUCCESS(add); EXAMPLE_REQUIRE(add_value.sum == 42);
  scale_request_value_t scale = {.has_value = true, .value = 7, .has_factor = true, .factor = 6};
  CALL(scale, scale); SUCCESS(scale); EXAMPLE_REQUIRE(scale_value.value == 42);
  self_test_request_value_t immediate = {.has_pattern = true, .pattern = UINT32_MAX};
  CALL(self_test, immediate); SUCCESS(self_test);
  EXAMPLE_REQUIRE(self_test_value.observed == (UINT32_MAX ^ UINT32_C(0xa5a5)));
  const unsigned slots[] = {0, 1};
  for (unsigned i = 0; i < 2; ++i) {
    start_self_test_request_value_t request = {.has_pattern = true, .pattern = 123};
    wl_kernel_call_handle_t handle;
    EXAMPLE_REQUIRE(device_kernel_start_self_test_start(&endpoints[0], &request, pair.now, 500,
      start_completed, (void *)&slots[i], &handle) == WL_OK);
  }
  await(&starts, 2);
  EXAMPLE_REQUIRE(start_results[0].outcome == WL_KERNEL_SUCCESS && job_id != 0);
  EXAMPLE_REQUIRE(start_results[1].outcome == WL_KERNEL_REJECTED && start_results[1].status == DEVICE_BUSY);
  await(&job_count, 1);
  EXAMPLE_REQUIRE(completed_job == job_id && observed == (123 ^ UINT32_C(0xa5a5)));
  device_telemetry_value_t message = {.has_sequence = true, .sequence = 42, .has_channels = true};
  message.channels[99] = UINT32_MAX;
  EXAMPLE_REQUIRE(device_kernel_send_device_telemetry(&endpoints[1], &message) == WL_OK);
  memset(&message, 0, sizeof(message)); await(&telemetry_count, 1);
  /* extension: client */
  example_pair_close(&pair);
  EXAMPLE_REQUIRE(get_info_value.name.length == 3 && memcmp(get_info_value.name.data, "a\0b", 3) == 0);
  puts("device/calculator handlers, application job ID, ordinary completion and retained results passed");
  return 0;
}
