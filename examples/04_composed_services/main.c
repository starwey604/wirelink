/* SPDX-License-Identifier: Apache-2.0 */
/* Application-specific RAM upload/status protocol, sharing ordinary kernel TX/RPC.
 * No flash write, reboot, driver thread or generic service/bulk framework. */
#include "product_kernel.h"
#include "loopback_owner.h"
#include "wirelink/crc.h"
/* Preserve the existing example message phase/status numbers. */
enum { PHASE_BEGIN = 1, PHASE_CHUNK = 2, PHASE_END = 3, PHASE_ABORT = 4 };
enum { STATUS_OK, STATUS_BUSY, STATUS_OUT_OF_ORDER, STATUS_CONFLICT,
       STATUS_INVALID, STATUS_WRITE_FAILED, STATUS_INTEGRITY_FAILED, STATUS_ABORTED };
static product_kernel_t endpoints[2];
static example_pair_t pair;
static uint8_t source[4096], destination[4096];
static struct {
  uint32_t id, crc;
  size_t offset;
  bool active, committed, finish, status_pending;
  bulk_status_value_t status;
} upload;
static unsigned commits, aborts, queries, telemetries, busy_writes, dropped_status, duplicate_chunks;
static bool busy_once = true, drop_once = true, query_pending, closing;
static unsigned closed_queries;
static uint32_t expected_transfer;
static bulk_status_value_t received_status;
static unsigned status_count, reboot_responses;
static uint32_t last_telemetry;
static void make_status(uint32_t phase, int32_t code) {
  upload.status = (bulk_status_value_t){.has_transfer_id = true, .transfer_id = upload.id,
    .has_phase = true, .phase = phase, .has_code = true, .code = code,
    .has_next_offset = true, .next_offset = upload.offset, .has_chunk_size = true, .chunk_size = 256};
  upload.status_pending = true;
}
static void command(void *user, const bulk_command_t *message) {
  (void)user;
  int32_t code = STATUS_OK;
  if (message->phase == PHASE_BEGIN) {
    if (message->transfer_id == 0 || message->total_length != sizeof(destination) || message->chunk_size != 256)
      code = STATUS_INVALID;
    else if (upload.active && message->transfer_id == upload.id) {
      if (message->crc32c != upload.crc) code = STATUS_CONFLICT;
    } else if (upload.active && !upload.committed) code = STATUS_BUSY;
    else {
      memset(destination, 0, sizeof(destination));
      upload.id = message->transfer_id; upload.crc = message->crc32c;
      upload.offset = 0; upload.active = true; upload.committed = false; upload.finish = false;
    }
  } else if (!upload.active || message->transfer_id != upload.id) code = STATUS_CONFLICT;
  else if (message->phase == PHASE_CHUNK) {
    const size_t size = message->data.length;
    if (size == 0 || size > 256 || message->offset > sizeof(destination) ||
        size > sizeof(destination) - (size_t)message->offset) code = STATUS_INVALID;
    else if (message->offset < upload.offset) {
      if (message->offset + size > upload.offset ||
          memcmp(destination + (size_t)message->offset, message->data.data, size)) code = STATUS_CONFLICT;
      else ++duplicate_chunks; /* Application idempotency after lost status. */
    } else if (message->offset != upload.offset || upload.committed) code = STATUS_OUT_OF_ORDER;
    else if (busy_once) { busy_once = false; ++busy_writes; code = STATUS_BUSY; }
    else { memcpy(destination + upload.offset, message->data.data, size); upload.offset += size; }
  } else if (message->phase == PHASE_END) {
    if (upload.offset != sizeof(destination)) code = STATUS_OUT_OF_ORDER;
    else if (!upload.committed) { upload.finish = true; return; }
  } else if (message->phase == PHASE_ABORT) {
    if (!upload.committed) ++aborts;
    upload.active = false; upload.finish = false; code = STATUS_ABORTED;
  } else code = STATUS_INVALID;
  make_status(message->phase, code);
}
static void status(void *user, const bulk_status_t *message) {
  (void)user;
  if (message->transfer_id != expected_transfer) return;
  EXAMPLE_REQUIRE(bulk_status_value_from_view(message, &received_status) == WL_CODEC_OK);
  ++status_count;
}
static void telemetry(void *user, const telemetry_t *message) {
  (void)user;
  EXAMPLE_REQUIRE(message->tick >= last_telemetry); last_telemetry = message->tick; ++telemetries;
}
static int32_t query(void *user, const query_request_t *request, query_response_value_t *response) {
  (void)user; (void)request;
  response->has_received = true; response->received = upload.offset; return 0;
}
static void queried(void *user, wl_kernel_result_t result, const query_response_t *response) {
  (void)user;
  if (result.outcome != WL_KERNEL_SUCCESS) {
    fprintf(stderr, "query terminal outcome=%d status=%d tick=%u closing=%u\n",
      (int)result.outcome, result.status, pair.now, closing ? 1U : 0U);
    EXAMPLE_REQUIRE(closing && result.outcome == WL_KERNEL_CLOSED);
    ++closed_queries; query_pending = false; return;
  }
  EXAMPLE_REQUIRE(response && response->received <= sizeof(destination));
  ++queries; query_pending = false;
}
static int32_t reboot(void *user, const reboot_request_t *request, reboot_response_value_t *response) {
  (void)user; (void)request; (void)response;
  return upload.committed ? 0 : STATUS_BUSY; /* Short business confirmation only. */
}
static void rebooted(void *user, wl_kernel_result_t result, const reboot_response_t *response) {
  (void)user;
  EXAMPLE_REQUIRE(result.outcome == WL_KERNEL_SUCCESS && response); ++reboot_responses;
}
static void advance(void) {
  /* Business work is explicit here, outside any borrowed callback. */
  if (upload.finish) {
    upload.finish = false;
    const bool valid = wl_crc32c(destination, sizeof(destination)) == upload.crc;
    if (valid && !upload.committed) { upload.committed = true; ++commits; }
    make_status(PHASE_END, valid ? STATUS_OK : STATUS_INTEGRITY_FAILED);
  }
  if (upload.status_pending) {
    if (drop_once && upload.status.phase == PHASE_CHUNK && upload.status.code == STATUS_OK) {
      drop_once = false; ++dropped_status; upload.status_pending = false;
    } else {
      const wl_err_t error = product_kernel_send_bulk_status(&endpoints[1], &upload.status);
      EXAMPLE_REQUIRE(error == WL_OK || error == WL_ERR_WOULD_BLOCK);
      if (error == WL_OK) upload.status_pending = false;
    }
  }
  if (pair.now % 5U == 0) {
    telemetry_value_t value = {.has_tick = true, .tick = pair.now};
    const wl_err_t error = product_kernel_send_telemetry(&endpoints[1], &value);
    EXAMPLE_REQUIRE(error == WL_OK || error == WL_ERR_WOULD_BLOCK);
  }
  if (!query_pending && pair.now % 10U == 0) {
    query_request_value_t request = {0}; wl_kernel_call_handle_t handle;
    const wl_err_t error = product_kernel_query_start(&endpoints[0], &request, pair.now, 500, queried, NULL, &handle);
    EXAMPLE_REQUIRE(error == WL_OK || error == WL_ERR_WOULD_BLOCK);
    if (error == WL_OK) query_pending = true;
  }
  example_pair_step(&pair);
}
static bulk_status_value_t exchange(uint32_t phase, uint32_t id, size_t offset, size_t length) {
  expected_transfer = id;
  for (unsigned attempt = 0; attempt < 4; ++attempt) {
    bulk_command_value_t value = {.has_phase = true, .phase = phase,
      .has_transfer_id = true, .transfer_id = id, .has_total_length = true, .total_length = sizeof(source),
      .has_chunk_size = true, .chunk_size = 256, .has_crc32c = true, .crc32c = wl_crc32c(source, sizeof(source)),
      .has_offset = true, .offset = offset, .has_data = true, .data = {.length = length}};
    EXAMPLE_REQUIRE(length <= sizeof(value.data.data) && offset + length <= sizeof(source));
    memcpy(value.data.data, source + offset, length);
    unsigned before = status_count;
    wl_err_t error;
    unsigned queued = 0;
    do { EXAMPLE_REQUIRE(++queued < 100); error = product_kernel_send_bulk_command(&endpoints[0], &value); if (error == WL_ERR_WOULD_BLOCK) advance(); }
    while (error == WL_ERR_WOULD_BLOCK);
    EXAMPLE_REQUIRE(error == WL_OK); memset(&value, 0, sizeof(value));
    for (unsigned pass = 0; pass < 40 && status_count == before; ++pass) advance();
    if (status_count != before) {
      EXAMPLE_REQUIRE(received_status.transfer_id == id && received_status.phase == phase);
      if (received_status.code != STATUS_BUSY) return received_status;
    }
    /* Application retry, with duplicate checks above; the kernel never retries. */
  }
  EXAMPLE_REQUIRE(false);
  return (bulk_status_value_t){0};
}
int main(void) {
  for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)(i * 17U + 3U);
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const product_kernel_handlers_t handlers = {.on_rpc_query = query, .on_rpc_reboot = reboot,
    .on_message_bulk_command = command, .on_message_bulk_status = status, .on_message_telemetry = telemetry};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(product_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  EXAMPLE_REQUIRE(exchange(PHASE_BEGIN, 1, 0, 0).code == STATUS_OK);
  for (size_t offset = 0; offset < sizeof(source); offset += 256) {
    bulk_status_value_t result = exchange(PHASE_CHUNK, 1, offset, 256);
    EXAMPLE_REQUIRE(result.code == STATUS_OK && result.next_offset == offset + 256);
  }
  EXAMPLE_REQUIRE(exchange(PHASE_END, 1, sizeof(source), 0).code == STATUS_OK);
  EXAMPLE_REQUIRE(exchange(PHASE_END, 1, sizeof(source), 0).code == STATUS_OK); /* No second commit. */
  EXAMPLE_REQUIRE(commits == 1 && busy_writes == 1 && dropped_status == 1 && duplicate_chunks == 1);
  EXAMPLE_REQUIRE(memcmp(source, destination, sizeof(source)) == 0 && queries && telemetries);
  reboot_request_value_t request = {0}; wl_kernel_call_handle_t handle;
  EXAMPLE_REQUIRE(product_kernel_reboot_start(&endpoints[0], &request, pair.now, 500, rebooted, NULL, &handle) == WL_OK);
  for (unsigned pass = 0; pass < 100 && !reboot_responses; ++pass) advance();
  EXAMPLE_REQUIRE(reboot_responses == 1);
  EXAMPLE_REQUIRE(exchange(PHASE_BEGIN, 2, 0, 0).code == STATUS_OK);
  EXAMPLE_REQUIRE(exchange(PHASE_CHUNK, 2, 0, 256).code == STATUS_OK);
  if (upload.active && !upload.committed) { ++aborts; upload.active = false; }
  closing = true;
  example_pair_close(&pair);
  EXAMPLE_REQUIRE(aborts == 1);
  printf("COMPOSED_SERVICES PASS bytes=4096 rpc=%u telemetry=%u busy=%u dropped_status=%u duplicate=%u commit=%u close_abort=%u closed_query=%u\n",
    queries, telemetries, busy_writes, dropped_status, duplicate_chunks, commits, aborts, closed_queries);
  return 0;
}
