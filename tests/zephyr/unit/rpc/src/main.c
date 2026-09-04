/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/rpc.h"

wl_rpc_err_t wl_rpc_test_server_set_next_generation(wl_rpc_server_t *server,
                                                    uint64_t generation);

struct client_fixture {
  wl_rpc_client_t client;
  wl_rpc_client_slot_t slots[2];
  uint8_t responses[2][8];
};

struct server_fixture {
  wl_rpc_server_t server;
  wl_rpc_server_pending_slot_t pending[2];
  wl_rpc_server_cache_slot_t cache[2];
  uint8_t responses[2][8];
};

static struct client_fixture clients;
static struct server_fixture servers;

static void client_init(uint32_t next_operation_id) {
  wl_rpc_client_config_t config;

  memset(&clients, 0, sizeof(clients));
  config = (wl_rpc_client_config_t){
      .slots = clients.slots,
      .slot_count = ARRAY_SIZE(clients.slots),
      .response_storage = &clients.responses[0][0],
      .response_storage_size = sizeof(clients.responses),
      .response_capacity_per_slot = sizeof(clients.responses[0]),
      .next_operation_id = next_operation_id,
  };
  zassert_equal(wl_rpc_client_init(&clients.client, &config), WL_RPC_OK);
}

static void server_init(wl_rpc_cache_policy_t policy,
                        uint32_t pending_timeout_ms, uint32_t cache_ttl_ms,
                        uint16_t pending_count, uint16_t cache_count) {
  wl_rpc_server_config_t config;

  memset(&servers, 0, sizeof(servers));
  config = (wl_rpc_server_config_t){
      .pending_slots = servers.pending,
      .pending_slot_count = pending_count,
      .cache_slots = servers.cache,
      .cache_slot_count = cache_count,
      .response_storage = &servers.responses[0][0],
      .response_storage_size = sizeof(servers.responses),
      .response_capacity_per_slot = sizeof(servers.responses[0]),
      .pending_timeout_ms = pending_timeout_ms,
      .cache_ttl_ms = cache_ttl_ms,
      .cache_policy = policy,
  };
  zassert_equal(wl_rpc_server_init(&servers.server, &config), WL_RPC_OK);
}

static wl_rpc_request_identity_t identity(uint32_t operation_id,
                                          uint64_t fingerprint) {
  return (wl_rpc_request_identity_t){
      .operation_id = operation_id,
      .request_message_id = 10U,
      .response_message_id = 11U,
      .request_fingerprint = fingerprint,
  };
}

static wl_rpc_server_response_t server_acquire_response(void) {
  wl_rpc_server_response_t response;

  zassert_equal(wl_rpc_server_response_acquire(&servers.server, &response),
                WL_RPC_OK);
  return response;
}

static void server_mark_next_sent(void) {
  wl_rpc_server_response_t response = server_acquire_response();

  zassert_equal(wl_rpc_server_response_sent(&servers.server, &response),
                WL_RPC_OK);
}

ZTEST(wirelink_rpc, test_init_validation_and_storage_boundaries) {
  wl_rpc_client_t uninitialized_client = {0};
  wl_rpc_client_slot_t client_slots[1];
  uint8_t client_response[4];
  wl_rpc_client_config_t client_config = {
      .slots = client_slots,
      .slot_count = ARRAY_SIZE(client_slots),
      .response_storage = client_response,
      .response_storage_size = sizeof(client_response) - 1U,
      .response_capacity_per_slot = sizeof(client_response),
  };
  wl_rpc_server_t uninitialized_server = {0};
  wl_rpc_server_pending_slot_t pending[1];
  wl_rpc_server_cache_slot_t cache[1];
  uint8_t server_response[4];
  wl_rpc_server_config_t server_config = {
      .pending_slots = pending,
      .pending_slot_count = ARRAY_SIZE(pending),
      .cache_slots = cache,
      .cache_slot_count = ARRAY_SIZE(cache),
      .response_storage = server_response,
      .response_storage_size = sizeof(server_response) - 1U,
      .response_capacity_per_slot = sizeof(server_response),
      .cache_policy = WL_RPC_CACHE_REJECT_NEW,
  };
  wl_rpc_server_expiry_t expiry;
  uint16_t timed_out;

  zassert_equal(wl_rpc_client_poll(&uninitialized_client, 0U, &timed_out),
                WL_RPC_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rpc_client_init(NULL, &client_config),
                WL_RPC_ERR_INVALID_ARG);
  zassert_equal(wl_rpc_client_init(&uninitialized_client, NULL),
                WL_RPC_ERR_INVALID_ARG);
  zassert_equal(wl_rpc_client_init(&uninitialized_client, &client_config),
                WL_RPC_ERR_INVALID_ARG);
  client_config.response_storage_size = sizeof(client_response);
  zassert_equal(wl_rpc_client_init(&uninitialized_client, &client_config),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_begin_with_id(&uninitialized_client, 1U, 10U, 11U,
                                            UINT32_C(0x80000000), 0U),
                WL_RPC_ERR_INVALID_ARG);

  zassert_equal(wl_rpc_server_poll(&uninitialized_server, 0U, &expiry),
                WL_RPC_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rpc_server_init(NULL, &server_config),
                WL_RPC_ERR_INVALID_ARG);
  zassert_equal(wl_rpc_server_init(&uninitialized_server, NULL),
                WL_RPC_ERR_INVALID_ARG);
  zassert_equal(wl_rpc_server_init(&uninitialized_server, &server_config),
                WL_RPC_ERR_INVALID_ARG);
  server_config.response_storage_size = sizeof(server_response);
  server_config.pending_timeout_ms = UINT32_C(0x80000000);
  zassert_equal(wl_rpc_server_init(&uninitialized_server, &server_config),
                WL_RPC_ERR_INVALID_ARG);
  server_config.pending_timeout_ms = 0U;
  zassert_equal(wl_rpc_server_init(&uninitialized_server, &server_config),
                WL_RPC_OK);
}

ZTEST(wirelink_rpc, test_client_capacity_id_wrap_and_conflict) {
  uint32_t first = 0U;
  uint32_t second = 0U;

  client_init(UINT32_MAX);
  zassert_equal(
      wl_rpc_client_begin(&clients.client, 10U, 11U, 100U, 0U, &first),
      WL_RPC_OK);
  zassert_equal(first, UINT32_MAX);
  zassert_equal(
      wl_rpc_client_begin(&clients.client, 10U, 11U, 100U, 0U, &second),
      WL_RPC_OK);
  zassert_equal(second, 1U);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, first, 10U, 11U, 100U, 0U),
      WL_RPC_ERR_OPERATION_CONFLICT);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 2U, 10U, 11U, 100U, 0U),
      WL_RPC_ERR_NO_SLOT);
  second = 99U;
  zassert_equal(
      wl_rpc_client_begin(&clients.client, 10U, 11U, 100U, 0U, &second),
      WL_RPC_ERR_NO_SLOT);
  zassert_equal(second, 0U);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 0U, 10U, 11U, 100U, 0U),
      WL_RPC_ERR_INVALID_ARG);
}

ZTEST(wirelink_rpc, test_client_link_success_then_application_success) {
  const uint8_t response[] = {1U, 2U, 3U};
  wl_event_t event = {
      .type = WL_EVT_TX_SUCCESS,
      .handle = 27U,
  };
  wl_rpc_client_result_t result;

  client_init(1U);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 8U, 10U, 11U, 100U, 4U),
      WL_RPC_OK);
  /* Early application completion cannot bypass link state. */
  zassert_equal(wl_rpc_client_on_response(&clients.client, 11U, 8U, 0, response,
                                          sizeof(response)),
                WL_RPC_ERR_INVALID_STATE);
  zassert_equal(wl_rpc_client_bind_tx(&clients.client, 8U, event.handle),
                WL_RPC_OK);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 9U, 10U, 11U, 100U, 4U),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_bind_tx(&clients.client, 9U, event.handle),
                WL_RPC_ERR_OPERATION_CONFLICT);
  zassert_equal(wl_rpc_client_on_tx_event(&clients.client, &event), WL_RPC_OK);
  zassert_equal(wl_rpc_client_get(&clients.client, 8U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_WAIT_RESPONSE);
  zassert_equal(result.link_delivery_confirmed, 1U);
  zassert_equal(wl_rpc_client_on_response(&clients.client, 12U, 8U, 0, response,
                                          sizeof(response)),
                WL_RPC_ERR_RESPONSE_MISMATCH);
  zassert_equal(wl_rpc_client_on_response(&clients.client, 11U, 8U, 0, response,
                                          sizeof(response)),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_get(&clients.client, 8U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_COMPLETED);
  zassert_equal(result.application_status, 0);
  zassert_equal(result.response_length, sizeof(response));
  zassert_mem_equal(result.response_data, response, sizeof(response));

  event.handle = 28U;
  zassert_equal(wl_rpc_client_bind_tx(&clients.client, 9U, event.handle),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_on_response(&clients.client, 11U, 9U, 0, response,
                                          sizeof(response)),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_get(&clients.client, 9U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_COMPLETED);
  zassert_equal(result.tx_handle, event.handle);
  zassert_equal(result.link_delivery_confirmed, 0U);
  zassert_equal(wl_rpc_client_on_tx_event(&clients.client, &event),
                WL_RPC_ERR_NOT_FOUND);
}

ZTEST(wirelink_rpc, test_client_link_failure_and_unreliable_application_error) {
  wl_event_t event = {
      .type = WL_EVT_TX_TIMEOUT,
      .handle = 9U,
  };
  wl_rpc_client_result_t result;
  const uint8_t detail[] = {0xa5U};

  client_init(1U);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 1U, 10U, 11U, 100U, 0U),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_bind_tx(&clients.client, 1U, event.handle),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_on_tx_event(&clients.client, &event), WL_RPC_OK);
  zassert_equal(wl_rpc_client_get(&clients.client, 1U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_LINK_FAILED);
  zassert_equal(result.link_result, WL_ERR_TIMEOUT);
  zassert_equal(result.link_delivery_confirmed, 0U);
  zassert_equal(wl_rpc_client_release(&clients.client, 1U), WL_RPC_OK);

  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 2U, 20U, 21U, 100U, 0U),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_tx_completed(&clients.client, 2U), WL_RPC_OK);
  zassert_equal(wl_rpc_client_on_response(&clients.client, 21U, 2U, -7, detail,
                                          sizeof(detail)),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_get(&clients.client, 2U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_APPLICATION_ERROR);
  zassert_equal(result.application_status, -7);
  zassert_equal(result.link_delivery_confirmed, 0U);
}

ZTEST(wirelink_rpc, test_client_deferred_start_releases_only_backpressure) {
  wl_rpc_client_result_t result;

  client_init(1U);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 1U, 10U, 11U, 100U, 0U),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_link_failed(&clients.client, 1U, WL_ERR_BUSY),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_release_deferred_start(&clients.client, 1U),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_get(&clients.client, 1U, &result),
                WL_RPC_ERR_NOT_FOUND);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 1U, 10U, 11U, 100U, 1U),
      WL_RPC_OK);
  zassert_equal(
      wl_rpc_client_link_failed(&clients.client, 1U, WL_ERR_WOULD_BLOCK),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_release_deferred_start(&clients.client, 1U),
                WL_RPC_OK);

  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 2U, 10U, 11U, 100U, 2U),
      WL_RPC_OK);
  zassert_equal(
      wl_rpc_client_link_failed(&clients.client, 2U, WL_ERR_NO_SPACE),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_release_deferred_start(&clients.client, 2U),
                WL_RPC_OK);

  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 3U, 10U, 11U, 100U, 3U),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_link_failed(&clients.client, 3U, WL_ERR_IO),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_release_deferred_start(&clients.client, 3U),
                WL_RPC_ERR_INVALID_STATE);
  zassert_equal(wl_rpc_client_get(&clients.client, 3U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_LINK_FAILED);
  zassert_equal(result.link_result, WL_ERR_IO);
}

ZTEST(wirelink_rpc, test_client_timeout_wrap_and_cancel_completion_race) {
  uint16_t timed_out = UINT16_MAX;
  wl_rpc_client_result_t result;

  client_init(1U);
  zassert_equal(wl_rpc_client_begin_with_id(&clients.client, 1U, 10U, 11U, 32U,
                                            UINT32_MAX - 15U),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_poll(&clients.client, 15U, &timed_out),
                WL_RPC_OK);
  zassert_equal(timed_out, 0U);
  zassert_equal(wl_rpc_client_poll(&clients.client, 16U, &timed_out),
                WL_RPC_OK);
  zassert_equal(timed_out, 1U);
  zassert_equal(wl_rpc_client_get(&clients.client, 1U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_TIMED_OUT);

  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 2U, 10U, 11U, 0U, 100U),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_tx_completed(&clients.client, 2U), WL_RPC_OK);
  zassert_equal(wl_rpc_client_cancel(&clients.client, 2U), WL_RPC_OK);
  zassert_equal(
      wl_rpc_client_on_response(&clients.client, 11U, 2U, 0, NULL, 0U),
      WL_RPC_ERR_INVALID_STATE);
  zassert_equal(wl_rpc_client_get(&clients.client, 2U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_CANCELLED);
}

ZTEST(wirelink_rpc,
      test_client_deadline_hint_min_disabled_wrap_and_no_mutation) {
  wl_rpc_deadline_hint_t hint;
  wl_rpc_client_result_t result;

  client_init(1U);
  zassert_equal(wl_rpc_client_get_deadline_hint(&clients.client, 0U, &hint),
                WL_RPC_OK);
  zassert_equal(hint.next_deadline_ms, WL_RPC_NO_DEADLINE_MS);
  zassert_equal(wl_rpc_client_begin_with_id(&clients.client, 1U, 10U, 11U, 0U,
                                            UINT32_MAX - 15U),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_get_deadline_hint(&clients.client, 0U, &hint),
                WL_RPC_OK);
  zassert_equal(hint.next_deadline_ms, WL_RPC_NO_DEADLINE_MS);

  client_init(1U);
  zassert_equal(wl_rpc_client_begin_with_id(&clients.client, 1U, 10U, 11U, 100U,
                                            UINT32_MAX - 15U),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_begin_with_id(&clients.client, 2U, 10U, 11U, 40U,
                                            UINT32_MAX - 5U),
                WL_RPC_OK);
  zassert_equal(wl_rpc_client_get_deadline_hint(&clients.client, 0U, &hint),
                WL_RPC_OK);
  zassert_equal(hint.next_deadline_ms, 34U);
  zassert_equal(wl_rpc_client_get_deadline_hint(&clients.client, 35U, &hint),
                WL_RPC_OK);
  zassert_equal(hint.next_deadline_ms, 0U);
  zassert_equal(wl_rpc_client_get(&clients.client, 2U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_QUEUED);
}

ZTEST(wirelink_rpc, test_client_oversize_response_is_terminal_runtime_error) {
  uint8_t response[9] = {0U};
  wl_rpc_client_result_t result;

  client_init(1U);
  zassert_equal(
      wl_rpc_client_begin_with_id(&clients.client, 1U, 10U, 11U, 100U, 0U),
      WL_RPC_OK);
  zassert_equal(wl_rpc_client_tx_completed(&clients.client, 1U), WL_RPC_OK);
  zassert_equal(wl_rpc_client_on_response(&clients.client, 11U, 1U, 0, response,
                                          sizeof(response)),
                WL_RPC_ERR_RESPONSE_TOO_LARGE);
  zassert_equal(wl_rpc_client_get(&clients.client, 1U, &result), WL_RPC_OK);
  zassert_equal(result.state, WL_RPC_CLIENT_APPLICATION_ERROR);
  zassert_equal(result.runtime_error, WL_RPC_ERR_RESPONSE_TOO_LARGE);
}

ZTEST(wirelink_rpc, test_server_pending_duplicate_replay_and_conflict) {
  wl_rpc_request_identity_t request = identity(1U, UINT64_C(0x1234));
  wl_rpc_request_identity_t conflict = identity(1U, UINT64_C(0x5678));
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;
  const uint8_t payload[] = {4U, 5U, 6U};

  server_init(WL_RPC_CACHE_REJECT_NEW, 0U, 0U, 2U, 2U);
  zassert_equal(wl_rpc_server_begin(&servers.server, &request, 0U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
  zassert_equal(wl_rpc_server_begin(&servers.server, &request, 1U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_PENDING_DUPLICATE);
  zassert_equal(wl_rpc_server_begin(&servers.server, &conflict, 1U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_CONFLICT);
  zassert_equal(wl_rpc_server_complete(&servers.server, &request, 0, payload,
                                       sizeof(payload), 2U, &response),
                WL_RPC_OK);
  zassert_mem_equal(response.response_data, payload, sizeof(payload));
  zassert_equal(wl_rpc_server_begin(&servers.server, &request, 3U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(response.response_length, sizeof(payload));
  zassert_mem_equal(response.response_data, payload, sizeof(payload));
  zassert_equal(wl_rpc_server_begin(&servers.server, &conflict, 3U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_CONFLICT);
}

ZTEST(wirelink_rpc, test_server_isolates_same_operation_across_peer_sessions) {
  wl_rpc_request_identity_t session_a = identity(1U, UINT64_C(0x1234));
  wl_rpc_request_identity_t session_b = session_a;
  wl_rpc_request_identity_t conflict = session_a;
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;

  session_a.peer_session_id = UINT64_C(0xAAA0000000000001);
  session_b.peer_session_id = UINT64_C(0xBBB0000000000002);
  conflict.peer_session_id = session_a.peer_session_id;
  conflict.request_fingerprint = UINT64_C(0x5678);

  server_init(WL_RPC_CACHE_REJECT_NEW, 0U, 0U, 2U, 2U);
  zassert_equal(wl_rpc_server_begin(&servers.server, &session_a, 0U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
  zassert_equal(wl_rpc_server_begin(&servers.server, &session_b, 1U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
  zassert_equal(wl_rpc_server_begin(&servers.server, &conflict, 2U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_CONFLICT);
  zassert_equal(wl_rpc_server_complete(&servers.server, &conflict, 0, NULL,
                                       0U, 2U, &response),
                WL_RPC_ERR_OPERATION_CONFLICT);

  zassert_equal(wl_rpc_server_complete(&servers.server, &session_a, 0, NULL,
                                       0U, 3U, &response),
                WL_RPC_OK);
  zassert_equal(response.identity.peer_session_id,
                session_a.peer_session_id);
  zassert_equal(wl_rpc_server_complete(&servers.server, &session_b, 0, NULL,
                                       0U, 4U, &response),
                WL_RPC_OK);
  zassert_equal(response.identity.peer_session_id,
                session_b.peer_session_id);

  zassert_equal(wl_rpc_server_begin(&servers.server, &session_a, 5U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(response.identity.peer_session_id,
                session_a.peer_session_id);
  zassert_equal(wl_rpc_server_begin(&servers.server, &session_b, 6U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(response.identity.peer_session_id,
                session_b.peer_session_id);
}

ZTEST(wirelink_rpc, test_server_capacity_reject_preserves_pending) {
  wl_rpc_request_identity_t first = identity(1U, 1U);
  wl_rpc_request_identity_t second = identity(2U, 2U);
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;
  uint8_t oversized[9] = {0U};

  server_init(WL_RPC_CACHE_REJECT_NEW, 0U, 0U, 2U, 1U);
  zassert_equal(
      wl_rpc_server_begin(&servers.server, &first, 0U, &disposition, &response),
      WL_RPC_OK);
  zassert_equal(
      wl_rpc_server_complete(&servers.server, &first, 0, NULL, 0U, 0U,
                             &response),
      WL_RPC_OK);
  zassert_equal(wl_rpc_server_begin(&servers.server, &second, 1U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_complete(&servers.server, &second, 0, oversized,
                                       sizeof(oversized), 1U, &response),
                WL_RPC_ERR_RESPONSE_TOO_LARGE);
  zassert_equal(
      wl_rpc_server_complete(&servers.server, &second, 0, NULL, 0U, 1U,
                             &response),
      WL_RPC_ERR_CACHE_FULL);
  zassert_equal(wl_rpc_server_begin(&servers.server, &second, 2U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_PENDING_DUPLICATE);
  zassert_equal(wl_rpc_server_abandon(&servers.server, &second), WL_RPC_OK);
}

ZTEST(wirelink_rpc, test_server_evict_oldest_and_reject_status) {
  wl_rpc_request_identity_t first = identity(1U, 1U);
  wl_rpc_request_identity_t second = identity(2U, 2U);
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;
  const uint8_t error_detail[] = {0xeeU};

  server_init(WL_RPC_CACHE_EVICT_OLDEST, 0U, 0U, 2U, 1U);
  zassert_equal(
      wl_rpc_server_begin(&servers.server, &first, 0U, &disposition, &response),
      WL_RPC_OK);
  zassert_equal(
      wl_rpc_server_complete(&servers.server, &first, 0, NULL, 0U, 1U,
                             &response),
      WL_RPC_OK);
  server_mark_next_sent();
  zassert_equal(wl_rpc_server_begin(&servers.server, &second, 2U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_reject(&servers.server, &second, -4, error_detail,
                                     sizeof(error_detail), 3U, &response),
                WL_RPC_OK);
  server_mark_next_sent();
  zassert_equal(response.application_status, -4);
  zassert_equal(wl_rpc_server_begin(&servers.server, &second, 4U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(
      wl_rpc_server_begin(&servers.server, &first, 4U, &disposition, &response),
      WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
}

ZTEST(wirelink_rpc, test_server_generation_wrap_evicts_true_oldest) {
  wl_rpc_request_identity_t requests[4];
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;
  uint32_t i;

  server_init(WL_RPC_CACHE_EVICT_OLDEST, 0U, 0U, 2U, 2U);
  zassert_equal(
      wl_rpc_test_server_set_next_generation(&servers.server, UINT64_MAX - 1U),
      WL_RPC_OK);
  for (i = 0U; i < ARRAY_SIZE(requests); ++i) {
    requests[i] = identity(i + 1U, i + 1U);
  }
  for (i = 0U; i < 3U; ++i) {
    zassert_equal(wl_rpc_server_begin(&servers.server, &requests[i], i,
                                      &disposition, &response),
                  WL_RPC_OK);
    zassert_equal(disposition, WL_RPC_SERVER_NEW);
    zassert_equal(wl_rpc_server_complete(&servers.server, &requests[i], 0,
                                         NULL, 0U, i, &response),
                  WL_RPC_OK);
    server_mark_next_sent();
  }

  /* Generations MAX-1, MAX, 0: request 1 must be the first eviction. */
  zassert_equal(wl_rpc_server_begin(&servers.server, &requests[3], 3U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
  zassert_equal(wl_rpc_server_begin(&servers.server, &requests[0], 3U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
  zassert_equal(wl_rpc_server_abandon(&servers.server, &requests[0]),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_begin(&servers.server, &requests[1], 3U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  server_mark_next_sent();

  zassert_equal(
      wl_rpc_server_complete(&servers.server, &requests[3], 0, NULL, 0U, 3U,
                             &response),
      WL_RPC_OK);
  server_mark_next_sent();
  /* With active generations MAX and 0, MAX is older and must go next. */
  zassert_equal(wl_rpc_server_begin(&servers.server, &requests[1], 4U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
  zassert_equal(wl_rpc_server_begin(&servers.server, &requests[2], 4U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(wl_rpc_server_begin(&servers.server, &requests[3], 4U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
}

ZTEST(wirelink_rpc, test_server_pending_and_cache_expiry_wrap) {
  wl_rpc_request_identity_t first = identity(1U, 1U);
  wl_rpc_request_identity_t second = identity(2U, 2U);
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;
  wl_rpc_request_identity_t expired;
  wl_rpc_server_expiry_t expiry;
  wl_rpc_deadline_hint_t hint;

  server_init(WL_RPC_CACHE_REJECT_NEW, 20U, 20U, 2U, 2U);
  zassert_equal(wl_rpc_server_begin(&servers.server, &first, UINT32_MAX - 15U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_begin(&servers.server, &second, UINT32_MAX - 15U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_complete(&servers.server, &second, 0, NULL, 0U,
                                       UINT32_MAX - 15U, &response),
                WL_RPC_OK);
  server_mark_next_sent();
  zassert_equal(wl_rpc_server_get_deadline_hint(&servers.server, 3U, &hint),
                WL_RPC_OK);
  zassert_equal(hint.next_deadline_ms, 1U);
  zassert_equal(wl_rpc_server_poll(&servers.server, 3U, &expiry), WL_RPC_OK);
  zassert_equal(expiry.pending_expired, 0U);
  zassert_equal(expiry.cache_expired, 0U);
  zassert_equal(wl_rpc_server_get_deadline_hint(&servers.server, 4U, &hint),
                WL_RPC_OK);
  zassert_equal(hint.next_deadline_ms, 0U);
  zassert_equal(wl_rpc_server_expired_acquire(&servers.server, 4U, &expired),
                WL_RPC_OK);
  zassert_mem_equal(&expired, &first, sizeof(first));
  zassert_equal(wl_rpc_server_poll(&servers.server, 4U, &expiry), WL_RPC_OK);
  zassert_equal(expiry.pending_expired, 0U);
  zassert_equal(expiry.cache_expired, 1U);
  zassert_equal(wl_rpc_server_abandon(&servers.server, &expired), WL_RPC_OK);
}

ZTEST(wirelink_rpc, test_server_owned_response_retry_lifecycle) {
  const uint8_t payload[] = {0x12U, 0x34U};
  wl_rpc_request_identity_t request = identity(31U, 47U);
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t completed;
  wl_rpc_server_response_t acquired;
  wl_event_t event = {.type = WL_EVT_TX_TIMEOUT, .handle = 81U};

  server_init(WL_RPC_CACHE_REJECT_NEW, 100U, 100U, 2U, 2U);
  zassert_equal(wl_rpc_server_begin(&servers.server, &request, 0U,
                                    &disposition, &completed),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_complete(&servers.server, &request, 0, payload,
                                       sizeof(payload), 1U, &completed),
                WL_RPC_OK);
  acquired = server_acquire_response();
  zassert_equal(acquired.generation, completed.generation);
  zassert_mem_equal(acquired.response_data, payload, sizeof(payload));
  zassert_equal(wl_rpc_server_response_submitted(&servers.server, &acquired,
                                                 event.handle),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_response_acquire(&servers.server, &completed),
                WL_RPC_ERR_NOT_FOUND);

  zassert_equal(wl_rpc_server_on_tx_event(&servers.server, &event), WL_RPC_OK);
  zassert_equal(wl_rpc_server_response_acquire(&servers.server, &completed),
                WL_RPC_ERR_NOT_FOUND);
  zassert_equal(wl_rpc_server_begin(&servers.server, &request, 2U,
                                    &disposition, &completed),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  acquired = server_acquire_response();
  zassert_equal(wl_rpc_server_response_defer(&servers.server, &acquired),
                WL_RPC_OK);
  acquired = server_acquire_response();
  zassert_equal(wl_rpc_server_response_submitted(&servers.server, &acquired,
                                                 82U),
                WL_RPC_OK);
  event.type = WL_EVT_TX_SUCCESS;
  event.handle = 82U;
  zassert_equal(wl_rpc_server_on_tx_event(&servers.server, &event), WL_RPC_OK);
  zassert_equal(wl_rpc_server_response_acquire(&servers.server, &completed),
                WL_RPC_ERR_NOT_FOUND);

  zassert_equal(wl_rpc_server_begin(&servers.server, &request, 3U,
                                    &disposition, &completed),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_REPLAY);
  acquired = server_acquire_response();
  zassert_equal(wl_rpc_server_response_sent(&servers.server, &acquired),
                WL_RPC_OK);
}

static void record_cancelled_handle(void *context, wl_tx_handle_t handle) {
  wl_tx_handle_t *cancelled = context;

  *cancelled = handle;
}

ZTEST(wirelink_rpc, test_server_discards_one_peer_session) {
  wl_rpc_request_identity_t pending = identity(51U, 61U);
  wl_rpc_request_identity_t in_flight = identity(52U, 62U);
  wl_rpc_request_identity_t other = identity(53U, 63U);
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;
  wl_rpc_server_discard_result_t discarded;
  wl_tx_handle_t cancelled = 0U;

  pending.peer_session_id = 7U;
  in_flight.peer_session_id = 7U;
  other.peer_session_id = 8U;
  server_init(WL_RPC_CACHE_REJECT_NEW, 100U, 100U, 2U, 2U);
  zassert_equal(wl_rpc_server_begin(&servers.server, &pending, 0U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_begin(&servers.server, &in_flight, 0U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_complete(&servers.server, &in_flight, 0, NULL,
                                       0U, 1U, &response),
                WL_RPC_OK);
  response = server_acquire_response();
  zassert_equal(wl_rpc_server_response_submitted(&servers.server, &response,
                                                 91U),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_begin(&servers.server, &other, 0U, &disposition,
                                    &response),
                WL_RPC_OK);

  zassert_equal(wl_rpc_server_discard_session(
                    &servers.server, 7U, record_cancelled_handle, &cancelled,
                    &discarded),
                WL_RPC_OK);
  zassert_equal(discarded.pending_discarded, 1U);
  zassert_equal(discarded.responses_discarded, 1U);
  zassert_equal(discarded.tx_cancel_requested, 1U);
  zassert_equal(cancelled, 91U);
  zassert_equal(wl_rpc_server_on_tx_event(
                    &servers.server,
                    &(wl_event_t){.type = WL_EVT_TX_SUCCESS, .handle = 91U}),
                WL_RPC_ERR_NOT_FOUND);
  zassert_equal(wl_rpc_server_begin(&servers.server, &pending, 2U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_NEW);
  zassert_equal(wl_rpc_server_begin(&servers.server, &other, 2U, &disposition,
                                    &response),
                WL_RPC_OK);
  zassert_equal(disposition, WL_RPC_SERVER_PENDING_DUPLICATE);
}

ZTEST(wirelink_rpc, test_server_zero_handle_terminal_event_is_unrelated) {
  wl_event_t event = {.type = WL_EVT_TX_SUCCESS, .handle = 0U};

  server_init(WL_RPC_CACHE_REJECT_NEW, 100U, 100U, 1U, 1U);
  zassert_equal(wl_rpc_server_on_tx_event(&servers.server, &event),
                WL_RPC_ERR_NOT_FOUND);
}

ZTEST(wirelink_rpc, test_server_exposes_expired_identity_before_release) {
  wl_rpc_request_identity_t request = identity(41U, 59U);
  wl_rpc_request_identity_t expired;
  wl_rpc_server_disposition_t disposition;
  wl_rpc_server_response_t response;
  wl_rpc_deadline_hint_t hint;

  server_init(WL_RPC_CACHE_REJECT_NEW, 10U, 100U, 1U, 1U);
  zassert_equal(wl_rpc_server_begin(&servers.server, &request, 5U,
                                    &disposition, &response),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_expired_acquire(&servers.server, 14U, &expired),
                WL_RPC_ERR_NOT_FOUND);
  zassert_equal(wl_rpc_server_expired_acquire(&servers.server, 15U, &expired),
                WL_RPC_OK);
  zassert_mem_equal(&expired, &request, sizeof(request));
  zassert_equal(wl_rpc_server_expired_acquire(&servers.server, 15U, &expired),
                WL_RPC_ERR_NOT_FOUND);
  zassert_equal(wl_rpc_server_get_deadline_hint(&servers.server, 15U, &hint),
                WL_RPC_OK);
  zassert_equal(hint.next_deadline_ms, WL_RPC_NO_DEADLINE_MS);
  zassert_equal(wl_rpc_server_reject(&servers.server, &request, -1, NULL, 0U,
                                     15U, &response),
                WL_RPC_OK);
  server_mark_next_sent();
}

ZTEST_SUITE(wirelink_rpc, NULL, NULL, NULL, NULL, NULL);
