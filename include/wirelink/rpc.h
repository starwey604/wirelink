/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_RPC_H_
#define INCLUDE_WIRELINK_RPC_H_

#include <stddef.h>
#include <stdint.h>

#include "wirelink/wirelink.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RPC errors are deliberately disjoint from the wl_err_t link error domain. */
typedef int32_t wl_rpc_err_t;
enum {
  WL_RPC_OK = 0,
  WL_RPC_ERR_INVALID_ARG = -1000,
  WL_RPC_ERR_NOT_INITIALIZED = -1001,
  WL_RPC_ERR_NO_SLOT = -1002,
  WL_RPC_ERR_OPERATION_CONFLICT = -1003,
  WL_RPC_ERR_NOT_FOUND = -1004,
  WL_RPC_ERR_INVALID_STATE = -1005,
  WL_RPC_ERR_RESPONSE_MISMATCH = -1006,
  WL_RPC_ERR_RESPONSE_TOO_LARGE = -1007,
  WL_RPC_ERR_CACHE_FULL = -1008,
};

const char *wl_rpc_err_str(wl_rpc_err_t error);

typedef int32_t wl_rpc_client_state_t;
enum {
  WL_RPC_CLIENT_FREE = 0,
  WL_RPC_CLIENT_QUEUED,
  WL_RPC_CLIENT_LINK_PENDING,
  WL_RPC_CLIENT_WAIT_RESPONSE,
  WL_RPC_CLIENT_COMPLETED,
  WL_RPC_CLIENT_LINK_FAILED,
  WL_RPC_CLIENT_TIMED_OUT,
  WL_RPC_CLIENT_CANCELLED,
  WL_RPC_CLIENT_APPLICATION_ERROR,
};

typedef int32_t wl_rpc_cache_policy_t;
enum {
  /* A full cache rejects completion until TTL expiry frees an entry. */
  WL_RPC_CACHE_REJECT_NEW = 0,
  /* The oldest cached response is discarded. Exactly-once replay then ends for
     it. */
  WL_RPC_CACHE_EVICT_OLDEST = 1,
};

typedef int32_t wl_rpc_server_disposition_t;
enum {
  WL_RPC_SERVER_NEW = 0,
  WL_RPC_SERVER_PENDING_DUPLICATE,
  WL_RPC_SERVER_REPLAY,
  WL_RPC_SERVER_CONFLICT,
};

/*
 * The private representations are fixed-size ABI storage. Applications allocate
 * arrays of slots but must not inspect or copy initialized objects.
 */
#define WL_RPC_CLIENT_STORAGE_SIZE 64U
#define WL_RPC_CLIENT_SLOT_STORAGE_SIZE 64U
#define WL_RPC_SERVER_STORAGE_SIZE 96U
#define WL_RPC_SERVER_PENDING_SLOT_STORAGE_SIZE 48U
#define WL_RPC_SERVER_CACHE_SLOT_STORAGE_SIZE 64U

typedef union wl_rpc_client {
  max_align_t align;
  uint8_t private_bytes[WL_RPC_CLIENT_STORAGE_SIZE];
} wl_rpc_client_t;

typedef union wl_rpc_client_slot {
  max_align_t align;
  uint8_t private_bytes[WL_RPC_CLIENT_SLOT_STORAGE_SIZE];
} wl_rpc_client_slot_t;

typedef union wl_rpc_server {
  max_align_t align;
  uint8_t private_bytes[WL_RPC_SERVER_STORAGE_SIZE];
} wl_rpc_server_t;

typedef union wl_rpc_server_pending_slot {
  max_align_t align;
  uint8_t private_bytes[WL_RPC_SERVER_PENDING_SLOT_STORAGE_SIZE];
} wl_rpc_server_pending_slot_t;

typedef union wl_rpc_server_cache_slot {
  max_align_t align;
  uint8_t private_bytes[WL_RPC_SERVER_CACHE_SLOT_STORAGE_SIZE];
} wl_rpc_server_cache_slot_t;

typedef struct wl_rpc_client_config {
  wl_rpc_client_slot_t *slots;
  uint16_t slot_count;
  uint8_t *response_storage;
  size_t response_storage_size;
  uint16_t response_capacity_per_slot;
  /* Zero selects operation ID 1. Allocation always skips reserved ID zero. */
  uint32_t next_operation_id;
} wl_rpc_client_config_t;

typedef struct wl_rpc_client_result {
  uint32_t operation_id;
  uint16_t request_message_id;
  uint16_t response_message_id;
  wl_rpc_client_state_t state;
  wl_tx_handle_t tx_handle;
  /* A wl_err_t or adapter result; separate from runtime_error. */
  int32_t link_result;
  /* Zero is application success; other values belong to the service schema. */
  int32_t application_status;
  wl_rpc_err_t runtime_error;
  /* One only after a reliable WL_EVT_TX_SUCCESS; local completion leaves zero.
   */
  uint8_t link_delivery_confirmed;
  const uint8_t *response_data;
  size_t response_length;
} wl_rpc_client_result_t;

#define WL_RPC_NO_DEADLINE_MS UINT32_MAX
typedef struct wl_rpc_deadline_hint {
  /* Relative to the now_ms passed to get_deadline_hint(). */
  uint32_t next_deadline_ms;
} wl_rpc_deadline_hint_t;

wl_rpc_err_t wl_rpc_client_init(wl_rpc_client_t *client,
                                const wl_rpc_client_config_t *config);

/* Allocate a nonzero operation ID that does not collide with a retained slot.
 */
wl_rpc_err_t wl_rpc_client_begin(wl_rpc_client_t *client,
                                 uint16_t request_message_id,
                                 uint16_t response_message_id,
                                 uint32_t timeout_ms, wl_time_ms_t now_ms,
                                 uint32_t *out_operation_id);

/* Use a product-provided operation ID. It must remain unique until release. */
wl_rpc_err_t wl_rpc_client_begin_with_id(
    wl_rpc_client_t *client, uint32_t operation_id, uint16_t request_message_id,
    uint16_t response_message_id, uint32_t timeout_ms, wl_time_ms_t now_ms);

/* Bind the handle returned by wl_send_reliable() after request
 * encoding/sending. */
wl_rpc_err_t wl_rpc_client_bind_tx(wl_rpc_client_t *client,
                                   uint32_t operation_id,
                                   wl_tx_handle_t tx_handle);

/*
 * Enter WAIT_RESPONSE after an uncorrelated/local TX completion. This is the
 * normal path for unreliable requests and does not confirm peer delivery.
 */
wl_rpc_err_t wl_rpc_client_tx_completed(wl_rpc_client_t *client,
                                        uint32_t operation_id);

/* Report a send failure that occurred before a Wirelink handle was bound. */
wl_rpc_err_t wl_rpc_client_link_failed(wl_rpc_client_t *client,
                                       uint32_t operation_id,
                                       int32_t link_result);

/*
 * Consume only TX terminal events. LINK success advances to WAIT_RESPONSE; it
 * never creates application success. Returns NOT_FOUND for an unrelated handle.
 */
wl_rpc_err_t wl_rpc_client_on_tx_event(wl_rpc_client_t *client,
                                       const wl_event_t *event);

/*
 * Correlate a decoded application response. A matching response may complete
 * LINK_PENDING before its independent core TX transaction terminates. The
 * payload is moved into caller-supplied storage and retained until release.
 */
wl_rpc_err_t wl_rpc_client_on_response(wl_rpc_client_t *client,
                                       uint16_t response_message_id,
                                       uint32_t operation_id,
                                       int32_t application_status,
                                       const uint8_t *response_payload,
                                       size_t response_length);

/* Advance all end-to-end deadlines. Returns the number newly timed out. */
wl_rpc_err_t wl_rpc_client_poll(wl_rpc_client_t *client, wl_time_ms_t now_ms,
                                uint16_t *out_timed_out);

/* Side-effect free: zero means due, WL_RPC_NO_DEADLINE_MS means none. */
wl_rpc_err_t wl_rpc_client_get_deadline_hint(const wl_rpc_client_t *client,
                                             wl_time_ms_t now_ms,
                                             wl_rpc_deadline_hint_t *out_hint);

/* Best effort: the returned result exposes a bound handle for wl_tx_cancel().
 */
wl_rpc_err_t wl_rpc_client_cancel(wl_rpc_client_t *client,
                                  uint32_t operation_id);

/* The returned response pointer remains valid until this operation is released.
 */
wl_rpc_err_t wl_rpc_client_get(const wl_rpc_client_t *client,
                               uint32_t operation_id,
                               wl_rpc_client_result_t *out_result);

/* Only terminal operations may be released and their ID reused locally. */
wl_rpc_err_t wl_rpc_client_release(wl_rpc_client_t *client,
                                   uint32_t operation_id);

typedef struct wl_rpc_request_identity {
  uint32_t operation_id;
  uint16_t request_message_id;
  uint16_t response_message_id;
  /*
   * A stable application-supplied fingerprint of the canonical request. The
   * server treats identity fields as exact; the caller owns collision quality.
   */
  uint64_t request_fingerprint;
} wl_rpc_request_identity_t;

typedef struct wl_rpc_server_config {
  wl_rpc_server_pending_slot_t *pending_slots;
  uint16_t pending_slot_count;
  wl_rpc_server_cache_slot_t *cache_slots;
  uint16_t cache_slot_count;
  uint8_t *response_storage;
  size_t response_storage_size;
  uint16_t response_capacity_per_slot;
  /* Zero disables timed expiry. Nonzero values must be below 2^31 ms. */
  uint32_t pending_timeout_ms;
  uint32_t cache_ttl_ms;
  wl_rpc_cache_policy_t cache_policy;
} wl_rpc_server_config_t;

typedef struct wl_rpc_server_response {
  wl_rpc_request_identity_t identity;
  int32_t application_status;
  const uint8_t *response_data;
  size_t response_length;
} wl_rpc_server_response_t;

typedef struct wl_rpc_server_expiry {
  uint16_t pending_expired;
  uint16_t cache_expired;
} wl_rpc_server_expiry_t;

wl_rpc_err_t wl_rpc_server_init(wl_rpc_server_t *server,
                                const wl_rpc_server_config_t *config);

/*
 * NEW reserves a pending slot. PENDING_DUPLICATE suppresses re-execution.
 * REPLAY returns cached response bytes. CONFLICT means the operation ID is in
 * use by a different exact identity. All four are successful classifications.
 */
wl_rpc_err_t wl_rpc_server_begin(wl_rpc_server_t *server,
                                 const wl_rpc_request_identity_t *identity,
                                 wl_time_ms_t now_ms,
                                 wl_rpc_server_disposition_t *out_disposition,
                                 wl_rpc_server_response_t *out_replay);

/* Complete an asynchronous or synchronous operation and cache/move its
 * response. */
wl_rpc_err_t wl_rpc_server_complete(wl_rpc_server_t *server,
                                    uint32_t operation_id,
                                    int32_t application_status,
                                    const uint8_t *response_payload,
                                    size_t response_length, wl_time_ms_t now_ms,
                                    wl_rpc_server_response_t *out_response);

/* Application rejection is a completed, replayable nonzero status. */
wl_rpc_err_t wl_rpc_server_reject(wl_rpc_server_t *server,
                                  uint32_t operation_id,
                                  int32_t application_status,
                                  const uint8_t *response_payload,
                                  size_t response_length, wl_time_ms_t now_ms,
                                  wl_rpc_server_response_t *out_response);

/* Drop pending metadata without manufacturing or caching a response. */
wl_rpc_err_t wl_rpc_server_abandon(wl_rpc_server_t *server,
                                   uint32_t operation_id);

/* Expire pending operations and cached replay entries with wrap-safe time. */
wl_rpc_err_t wl_rpc_server_poll(wl_rpc_server_t *server, wl_time_ms_t now_ms,
                                wl_rpc_server_expiry_t *out_expiry);

/* Includes both pending timeouts and cache TTLs without expiring either. */
wl_rpc_err_t wl_rpc_server_get_deadline_hint(const wl_rpc_server_t *server,
                                             wl_time_ms_t now_ms,
                                             wl_rpc_deadline_hint_t *out_hint);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_WIRELINK_RPC_H_ */
