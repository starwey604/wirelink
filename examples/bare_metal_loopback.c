/* SPDX-License-Identifier: Apache-2.0 */
#include "demo_kernel.h"
#include "wirelink/packet_loopback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static demo_kernel_t endpoints[2];
static wl_packet_port_t ports[2];
static uint8_t receive_storage[2][2][256];
static wl_packet_loopback_t transport;
static unsigned completions, messages, executions;

static void require(int condition) {
  if (!condition) {
    fputs("loopback contract failed\n", stderr);
    exit(EXIT_FAILURE);
  }
}
static int32_t execute(void *user, const request_t *request, response_value_t *response) {
  (void)user;
  ++executions;
  response->has_output = true;
  response->output = request->input;
  return 0;
}
static void completed(void *user, wl_kernel_result_t result, const response_t *response) {
  (void)user;
  /* response is borrowed only until this callback returns. */
  require(result.outcome == WL_KERNEL_SUCCESS && response && response->output == INT32_MIN);
  ++completions;
}
static void received(void *user, const event_t *event) {
  (void)user;
  require(event->data.length == 3U && memcmp(event->data.data, "abc", 3U) == 0);
  ++messages;
}
int main(void) {
  require(wl_packet_loopback_init(&transport, &ports[0], &ports[1], NULL, NULL) == WL_OK);
  for (unsigned side = 0; side < 2U; ++side) {
    /* A real integration supplies a fresh nonzero instance at each restart. */
    wl_packet_config_t config = {.instance = 1001U + side,
        .rx_storage = &receive_storage[side][0][0], .rx_unit_size = 256U,
        .rx_storage_size = sizeof(receive_storage[side]),
        .submit = wl_packet_loopback_submit, .notify = wl_packet_loopback_notify,
        .user = &transport.directions[side]};
    require(wl_packet_init(&ports[side], &config) == WL_OK);
    demo_kernel_handlers_t handlers = {.on_rpc_execute = execute, .on_message_event = received};
    require(demo_kernel_init(&endpoints[side], &ports[side], &handlers, NULL, NULL) == WL_OK);
    for (unsigned slot = 0; slot < 4U; ++slot) {
      request_value_t request = {.has_input = true, .input = INT32_MIN};
      wl_kernel_call_handle_t call;
      require(demo_kernel_execute_start(&endpoints[side], &request, 0U, 500U,
          completed, NULL, &call) == WL_OK);
      /* Admission copied the request; this object is no longer borrowed. */
      memset(&request, 0, sizeof(request));
    }
    event_value_t event = {.has_data = true, .data = {.length = 3U, .data = {'a', 'b', 'c'}}};
    require(demo_kernel_send_event(&endpoints[side], &event) == WL_OK);
    memset(&event, 0, sizeof(event));
  }
  for (uint32_t now = 0; now < 500U && (completions != 8U || messages != 2U); ++now) {
    /* All endpoint operations/callbacks share this owner. No core clock/thread. */
    require(demo_kernel_step(&endpoints[0], now) == WL_OK);
    require(demo_kernel_step(&endpoints[1], now) == WL_OK);
    require(wl_packet_loopback_service(&transport, 2U) == WL_OK);
  }
  require(completions == 8U && executions == 8U && messages == 2U);
  for (unsigned side = 0; side < 2U; ++side)
    require(wl_kernel_close(&endpoints[side].core) == WL_OK);
  wl_packet_loopback_stop(&transport);
  for (unsigned side = 0; side < 2U; ++side) {
    require(demo_kernel_step(&endpoints[side], 500U) == WL_OK);
    require(wl_kernel_quiescent(&endpoints[side].core));
  }
  puts("8 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent");
  return 0;
}
