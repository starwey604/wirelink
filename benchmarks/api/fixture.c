/* SPDX-License-Identifier: Apache-2.0 */
#include "fixture.h"
#include "kernel_api_kernel.h"
#include "loopback_owner.h"
typedef struct { uint32_t expected; unsigned count; bool rejected; } job_t;
static struct {
  kernel_api_kernel_t endpoints[2];
  example_pair_t pair;
  job_t jobs[2][4];
  sample_value_t sample;
  unsigned completed, received;
  uint32_t sequence;
} fixture;
static int32_t query(void *user, const query_t *request, answer_value_t *response) {
  (void)user;
  if (request->sequence & UINT32_C(0x80000000)) return 7;
  response->has_sequence = true; response->sequence = request->sequence; return 0;
}
static void completed(void *user, wl_kernel_result_t result, const answer_t *view) {
  job_t *job = user;
  EXAMPLE_REQUIRE(++job->count == 1);
  EXAMPLE_REQUIRE(result.outcome == (job->rejected ? WL_KERNEL_REJECTED : WL_KERNEL_SUCCESS));
  if (job->rejected) EXAMPLE_REQUIRE(result.status == 7 && !view);
  else EXAMPLE_REQUIRE(view && view->sequence == job->expected);
  ++fixture.completed;
}
static void sample(void *user, const sample_t *view) {
  (void)user;
  EXAMPLE_REQUIRE(view->sequence == fixture.sequence && view->payload.length == fixture.sample.payload.length);
  for (size_t i = 0; i < view->payload.length; ++i)
    EXAMPLE_REQUIRE(view->payload.data[i] == (uint8_t)i);
  ++fixture.received;
}
void api_fixture_init(void) {
  memset(&fixture, 0, sizeof(fixture));
  example_pair_init(&fixture.pair, &fixture.endpoints[0].core, &fixture.endpoints[1].core);
  const kernel_api_kernel_handlers_t handlers = {.on_rpc_query = query, .on_message_sample = sample};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(kernel_api_kernel_init(&fixture.endpoints[side], &fixture.pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  fixture.sample.has_sequence = fixture.sample.has_payload = true;
  for (size_t i = 0; i < sizeof(fixture.sample.payload.data); ++i) fixture.sample.payload.data[i] = (uint8_t)i;
}
void api_fixture_rpc(int rejected) {
  fixture.completed = 0;
  for (unsigned side = 0; side < 2; ++side) for (unsigned slot = 0; slot < 4; ++slot) {
    job_t *job = &fixture.jobs[side][slot];
    *job = (job_t){.expected = 1 + 4 * side + slot, .rejected = rejected != 0};
    query_value_t request = {.has_sequence = true,
      .sequence = job->expected | (rejected ? UINT32_C(0x80000000) : 0)};
    wl_kernel_call_handle_t handle;
    EXAMPLE_REQUIRE(kernel_api_kernel_query_start(&fixture.endpoints[side], &request,
      fixture.pair.now, 1000, completed, job, &handle) == WL_OK);
    memset(&request, 0, sizeof(request));
  }
  example_pair_wait(&fixture.pair, &fixture.completed, 8);
}
void api_fixture_sample(size_t bytes) {
  EXAMPLE_REQUIRE(bytes <= sizeof(fixture.sample.payload.data));
  fixture.received = 0; fixture.sample.payload.length = bytes;
  fixture.sample.sequence = ++fixture.sequence;
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(kernel_api_kernel_send_sample(&fixture.endpoints[side], &fixture.sample) == WL_OK);
  example_pair_wait(&fixture.pair, &fixture.received, 2);
}
void api_fixture_idle(void) {
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(kernel_api_kernel_step(&fixture.endpoints[side], fixture.pair.now) == WL_OK);
}
void api_fixture_close(void) { example_pair_close(&fixture.pair); }
size_t api_fixture_endpoint_bytes(void) { return sizeof(kernel_api_kernel_t); }
size_t api_fixture_total_bytes(void) { return sizeof(fixture); }
