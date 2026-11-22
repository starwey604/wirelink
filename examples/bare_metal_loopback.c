/* SPDX-License-Identifier: Apache-2.0 */
#include "demo.h"
#include "wirelink/packet_loopback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static demo_node_t endpoints[2];
static wl_packet_loopback_t transport;
static unsigned completions, messages, executions;

static void require(int condition) {
  if (!condition) {
    fputs("loopback contract failed\n", stderr);
    exit(EXIT_FAILURE);
  }
}
static int32_t execute(void *user, const request_t *request, response_t *response) {
  (void)user;
  ++executions;
  response->output = request->input;
  return 0;
}
static void completed(void *user, wl_rpc_result_t result, const response_t *response) {
  (void)user;
  /* response is borrowed only until this callback returns. */
  require(result.outcome == WL_RPC_SUCCESS && response && response->output == INT32_MIN);
  ++completions;
}
static void received(void *user, const event_t *event) {
  (void)user;
  require(event->data.length == 3U && memcmp(event->data.data, "abc", 3U) == 0);
  ++messages;
}
int main(void) {
  require(wl_packet_loopback_init(&transport, NULL, NULL) == WL_OK);
  for (unsigned side = 0; side < 2U; ++side) {
    /* A real integration supplies a fresh nonzero instance at each restart. */
    /* [docs:loopback_setup] */
    wl_transport_t *link = wl_packet_loopback_transport(&transport, side);
    demo_handlers_t handlers = {.on_execute = execute, .on_event = received};
    require(demo_bind_instance(&endpoints[side], 1001U + side, link, &handlers, NULL) == WL_OK);
    /* [/docs:loopback_setup] */
    for (unsigned slot = 0; slot < WL_ENDPOINT_REPLIES; ++slot) {
      request_t request = {.input = INT32_MIN};
      wl_call_handle_t call;
      require(demo_call_execute_ex(&endpoints[side], &request, 0U, 500U,
          completed, NULL, &call) == WL_OK);
      /* Admission copied the request; this object is no longer borrowed. */
      memset(&request, 0, sizeof(request));
    }
    event_t event = {.data = {.length = 3U, .data = {'a', 'b', 'c'}}};
    require(demo_send_event(&endpoints[side], &event) == WL_OK);
    memset(&event, 0, sizeof(event));
  }
  const unsigned expected_rpcs = 2U * WL_ENDPOINT_REPLIES;
  for (uint32_t now = 0; now < 500U && (completions != expected_rpcs || messages != 2U); ++now) {
    /* All endpoint operations/callbacks share this owner. No core clock/thread. */
    require(wl_node_step(&endpoints[0].node, now) == WL_OK);
    require(wl_node_step(&endpoints[1].node, now) == WL_OK);
  }
  require(completions == expected_rpcs && executions == expected_rpcs && messages == 2U);
  for (unsigned side = 0; side < 2U; ++side)
    require(wl_node_close(&endpoints[side].node, 500U) == WL_OK);
  puts("6 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent");
  return 0;
}
