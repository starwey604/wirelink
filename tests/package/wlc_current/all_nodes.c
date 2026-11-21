/* SPDX-License-Identifier: Apache-2.0 */
#include "roles.h"
#include <stdlib.h>
static client_node_t client;
static server_node_t server;
static unsigned completed;
wl_err_t client_tx_write(client_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return server_rx_packet(&server, data, length);
}
wl_err_t server_tx_write(server_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return client_rx_packet(&client, data, length);
}
int32_t server_on_execute(server_node_t *self, const input_t *request, output_t *reply) {
  (void)self;
  reply->value = request->value + 1;
  return 0;
}
static void done(client_node_t *self, wl_rpc_result_t result, const output_t *reply) {
  (void)self;
  if (result.outcome != WL_RPC_SUCCESS || reply == NULL || reply->value != 42) abort();
  ++completed;
}
int main(void) {
  const input_t request = {.value = 41};
  if (client_init(&client, client_tx_write) || server_init(&server, server_tx_write) ||
      client_call_execute(&client, &request, done)) abort();
  for (unsigned i = 0; i < 10 && !completed; ++i)
    if (client_step(&client, i) || server_step(&server, i)) abort();
  if (completed != 1 || client_close(&client) || server_close(&server) ||
      client_step(&client, 10) || server_step(&server, 10) ||
      !client_quiescent(&client) || !server_quiescent(&server)) abort();
}
