/* SPDX-License-Identifier: Apache-2.0 */
#include "roles.h"
#include <stdlib.h>
static unsigned completed;
wl_err_t client_tx_write(client_node_t *self, const uint8_t *data, size_t length) {
  (void)self; (void)data; (void)length;
  return WL_OK;
}
static void done(void *user, wl_rpc_result_t result, const output_t *reply) {
  (void)user;
  if (result.outcome != WL_RPC_CANCELLED || reply != NULL) abort();
  ++completed;
}
int main(void) {
  client_node_t client;
  input_t request = {.value = 42};
  wl_call_handle_t handle;
  if (client_bind_sync(&client, client_tx_write) ||
      client_call_execute_ex(&client, &request, 0, 100, done, NULL, &handle) ||
      wl_node_cancel(&client.node, handle) || wl_node_step(&client.node, 0) ||
      completed != 1 || wl_node_close(&client.node, 0U) || wl_node_step(&client.node, 0) ||
      !wl_node_quiescent(&client.node)) abort();
  /* No server implementation or linker garbage collection is required. */
}
