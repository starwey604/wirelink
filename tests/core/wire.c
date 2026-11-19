/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/wire.h"

#define WL_FRAME_MAX_PAYLOAD 2048U

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WL_PACKET_ZTEST
#include <zephyr/ztest.h>
#define CHECK(x) zassert_true((x), "%s:%d: %s", __FILE__, __LINE__, #x)
#else
#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)
#endif

#include "rpc_vectors.h"

static void check_bad_rpc(const uint8_t *data, size_t length, uint8_t kind) {
  wl_rpc_metadata_t out, saved;
  memset(&out, 0xa5, sizeof(out));
  memcpy(&saved, &out, sizeof(out));
  CHECK(wl_wire_rpc_read(kind, data, length, &out) == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
}

static void test_rpc_vectors(void) {
  uint8_t bytes[32], saved[32];
  const uint32_t session = UINT32_C(0x12345678);
  for (size_t i = 0U; i < sizeof(rpc_vectors) / sizeof(rpc_vectors[0]); ++i) {
    size_t length = SIZE_MAX;
    CHECK(wl_wire_rpc_header_size(rpc_vectors[i].status) == rpc_vectors[i].length);
    memset(bytes, 0xa5, sizeof(bytes));
    CHECK(wl_wire_rpc_write(bytes, sizeof(bytes), session,
          rpc_vectors[i].operation, rpc_vectors[i].status, &length) == WL_OK);
    CHECK(length == rpc_vectors[i].length);
    CHECK(memcmp(bytes, rpc_vectors[i].bytes, length) == 0);
    CHECK(bytes[length] == 0xa5);
    uint8_t kind = rpc_vectors[i].status == 0 ? WL_WIRE_RPC_RESP : WL_WIRE_RPC_ERR;
    wl_rpc_metadata_t metadata;
    CHECK(wl_wire_rpc_read(kind, bytes, length, &metadata) == WL_OK);
    CHECK(metadata.session == session && metadata.operation == rpc_vectors[i].operation);
    CHECK(metadata.status == rpc_vectors[i].status && metadata.length == 0U);
    CHECK(metadata.body == bytes + length);
    for (size_t shorter = 0U; shorter < length; ++shorter)
      check_bad_rpc(bytes, shorter, kind);
    if (metadata.status != 0) {
      check_bad_rpc(bytes, length + 1U, WL_WIRE_RPC_ERR);
    } else {
      CHECK(wl_wire_rpc_read(WL_WIRE_RPC_REQ, bytes, length + 3U, &metadata) == WL_OK);
      CHECK(metadata.length == 3U && metadata.body == bytes + length);
      CHECK(wl_wire_rpc_read(WL_WIRE_RPC_RESP, bytes, length + 3U, &metadata) == WL_OK);
      CHECK(metadata.length == 3U && metadata.body == bytes + length);
    }
    memcpy(saved, bytes, sizeof(bytes));
    CHECK(wl_wire_rpc_write(bytes, length - 1U, session,
          rpc_vectors[i].operation, rpc_vectors[i].status, &length) == WL_ERR_BUF_TOO_SMALL);
    CHECK(length == 0U && memcmp(bytes, saved, sizeof(bytes)) == 0);
  }
  size_t length = SIZE_MAX;
  CHECK(wl_wire_rpc_write(bytes, sizeof(bytes), 0U, 1U, 0, &length) == WL_ERR_INVALID_ARG);
  CHECK(length == 0U);
  CHECK(wl_wire_rpc_write(bytes, sizeof(bytes), session, 0U, 0, &length) == WL_ERR_INVALID_ARG);
}

static void test_malformed_rpc(void) {
  uint8_t bytes[16] = {0};
  /* Zero operation, zero session, zero error status, trailing err bytes, invalid kind. */
  bytes[0] = 1U;
  check_bad_rpc(bytes, 8U, WL_WIRE_RPC_REQ);
  bytes[0] = 0U; bytes[4] = 2U;
  check_bad_rpc(bytes, 8U, WL_WIRE_RPC_REQ);
  bytes[0] = 1U;
  check_bad_rpc(bytes, 12U, WL_WIRE_RPC_ERR);
  bytes[8] = 1U;
  check_bad_rpc(bytes, 13U, WL_WIRE_RPC_ERR);
  check_bad_rpc(bytes, 8U, WL_WIRE_MSG);
  check_bad_rpc(bytes, 8U, 4U);
}

static void check_bad_frame(const uint8_t *data, size_t length, wl_err_t expected) {
  wl_wire_frame_t out, saved;
  memset(&out, 0xa5, sizeof(out));
  memcpy(&saved, &out, sizeof(out));
  CHECK(wl_wire_frame_read(data, length, &out) == expected);
  CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
}

static void test_native_none(void) {
  static uint8_t frame[WL_WIRE_NATIVE_HEADER + WL_FRAME_MAX_PAYLOAD + 1U];
  for (size_t i = WL_WIRE_NATIVE_HEADER; i < sizeof(frame); ++i)
    frame[i] = (uint8_t)i;
  /* Preserve every payload byte: the wire header writes only the 4B prefix. */
  const size_t lengths[] = {0U, 1U, 20U, 32U, 120U, 512U, 1032U, 2048U};
  for (size_t i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
    CHECK(wl_wire_native_none_seal(frame, sizeof(frame), 0x1234U, lengths[i]) == WL_OK);
    const uint8_t golden[] = {0x57U, 0x00U, 0x34U, 0x12U};
    CHECK(memcmp(frame, golden, sizeof(golden)) == 0);
    wl_wire_frame_t parsed;
    CHECK(wl_wire_native_none_read(frame, 4U + lengths[i], &parsed) == WL_OK);
    CHECK(parsed.message_id == 0x1234U && parsed.kind == WL_WIRE_MSG && parsed.length == lengths[i]);
    CHECK(parsed.payload == frame + 4U);
    for (size_t at = 4U; at < sizeof(frame); ++at) CHECK(frame[at] == (uint8_t)at);
  }
  for (uint8_t kind = WL_WIRE_MSG; kind <= WL_WIRE_RPC_ERR; ++kind) {
    CHECK(wl_wire_frame_seal(frame, sizeof(frame), kind, 0x1234U, 12U) == WL_OK);
    const uint8_t golden[] = {0x57U, kind, 0x34U, 0x12U};
    CHECK(memcmp(frame, golden, sizeof(golden)) == 0);
    wl_wire_frame_t parsed;
    CHECK(wl_wire_frame_read(frame, 16U, &parsed) == WL_OK);
    CHECK(parsed.message_id == 0x1234U && parsed.kind == kind && parsed.length == 12U);
  }
  uint8_t saved[4];
  memcpy(saved, frame, sizeof(saved));
  CHECK(wl_wire_frame_seal(frame, 5U, WL_WIRE_MSG, 1U, 2U) == WL_ERR_BUF_TOO_SMALL);
  CHECK(wl_wire_frame_seal(frame, sizeof(frame), WL_WIRE_MSG, 0U, 0U) == WL_ERR_INVALID_ARG);
  CHECK(wl_wire_frame_seal(frame, sizeof(frame), 4U, 1U, 0U) == WL_ERR_INVALID_ARG);
  CHECK(wl_wire_frame_seal(frame, sizeof(frame), WL_WIRE_MSG, 1U, 2049U) == WL_ERR_PAYLOAD_TOO_LONG);
  CHECK(memcmp(frame, saved, sizeof(saved)) == 0);
  for (size_t length = 0U; length < 4U; ++length)
    check_bad_frame(frame, length, WL_ERR_BAD_FRAME);
  check_bad_frame(frame, sizeof(frame), WL_ERR_FRAME_TOO_LONG);
  frame[0] = 0xa4U;
  check_bad_frame(frame, 4U, WL_ERR_BAD_FRAME);
  frame[0] = 0x57U; frame[1] = 4U;
  check_bad_frame(frame, 4U, WL_ERR_NOT_SUPPORTED);
  frame[1] = 0U; frame[2] = 0U; frame[3] = 0U;
  check_bad_frame(frame, 4U, WL_ERR_BAD_FRAME);
}

#ifdef WL_PACKET_ZTEST
ZTEST(static_endpoint_wire, rpc_vectors) { test_rpc_vectors(); }
ZTEST(static_endpoint_wire, malformed_rpc) { test_malformed_rpc(); }
ZTEST(static_endpoint_wire, native_none) { test_native_none(); }
ZTEST_SUITE(static_endpoint_wire, NULL, NULL, NULL, NULL, NULL);
#else
int main(void) {
  test_rpc_vectors();
  test_malformed_rpc();
  test_native_none();
  return 0;
}
#endif
