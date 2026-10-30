/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel_wire.h"
#include "wirelink/frame.h"

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

static void check_bad_rpc(const uint8_t *data, size_t length, int request) {
  wl_kernel_rpc_metadata_t out, saved;
  memset(&out, 0xa5, sizeof(out));
  memcpy(&saved, &out, sizeof(out));
  CHECK(wl_kernel_rpc_read(data, length, request, &out) == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
}

static void test_rpc_vectors(void) {
  uint8_t bytes[32], saved[32];
  const uint64_t session = UINT64_C(0x123456789abcdef0);
  for (size_t i = 0U; i < sizeof(rpc_vectors) / sizeof(rpc_vectors[0]); ++i) {
    size_t length = SIZE_MAX;
    CHECK(wl_kernel_rpc_header_size(rpc_vectors[i].operation, rpc_vectors[i].status) ==
          rpc_vectors[i].length);
    memset(bytes, 0xa5, sizeof(bytes));
    CHECK(wl_kernel_rpc_write(bytes, sizeof(bytes), session,
          rpc_vectors[i].operation, rpc_vectors[i].status, &length) == WL_OK);
    CHECK(length == rpc_vectors[i].length);
    CHECK(memcmp(bytes, rpc_vectors[i].bytes, length) == 0);
    CHECK(bytes[length] == 0xa5);
    wl_kernel_rpc_metadata_t metadata;
    CHECK(wl_kernel_rpc_read(bytes, length, 0, &metadata) == WL_OK);
    CHECK(metadata.session == session && metadata.operation == rpc_vectors[i].operation);
    CHECK(metadata.status == rpc_vectors[i].status && metadata.length == 0U);
    CHECK(metadata.body == bytes + length);
    for (size_t shorter = 0U; shorter < length; ++shorter)
      check_bad_rpc(bytes, shorter, 0);
    if (metadata.status != 0) {
      check_bad_rpc(bytes, length, 1);
      check_bad_rpc(bytes, length + 1U, 0);
    } else {
      CHECK(wl_kernel_rpc_read(bytes, length + 3U, 1, &metadata) == WL_OK);
      CHECK(metadata.length == 3U && metadata.body == bytes + length);
    }
    memcpy(saved, bytes, sizeof(bytes));
    CHECK(wl_kernel_rpc_write(bytes, length - 1U, session,
          rpc_vectors[i].operation, rpc_vectors[i].status, &length) == WL_ERR_BUF_TOO_SMALL);
    CHECK(length == 0U && memcmp(bytes, saved, sizeof(bytes)) == 0);
  }
  size_t length = SIZE_MAX;
  CHECK(wl_kernel_rpc_write(bytes, sizeof(bytes), 0U, 1U, 0, &length) == WL_ERR_INVALID_ARG);
  CHECK(length == 0U);
  CHECK(wl_kernel_rpc_write(bytes, sizeof(bytes), session, 0U, 0, &length) == WL_ERR_INVALID_ARG);
  CHECK(wl_kernel_rpc_header_size(0U, 0) == 0U);
}

static void test_malformed_rpc(void) {
  uint8_t bytes[20] = {0};
  bytes[7] = 1U;
  /* Zero operation, zero session, nonminimal operation and overflowing op. */
  check_bad_rpc(bytes, 9U, 0);
  bytes[7] = 0U; bytes[8] = 2U;
  check_bad_rpc(bytes, 9U, 0);
  bytes[7] = 1U; bytes[8] = 0x82U; bytes[9] = 0U;
  check_bad_rpc(bytes, 10U, 0);
  bytes[8] = 0x80U; bytes[9] = 0x80U; bytes[10] = 0x80U;
  bytes[11] = 0x80U; bytes[12] = 32U;
  check_bad_rpc(bytes, 13U, 0);
  bytes[12] = 0x80U; bytes[13] = 1U;
  check_bad_rpc(bytes, 14U, 0);
  memset(bytes + 8U, 0, sizeof(bytes) - 8U);
  bytes[8] = 3U; /* Rejection with a forbidden zero status. */
  check_bad_rpc(bytes, 13U, 0);
}

static void check_bad_frame(const uint8_t *data, size_t length, wl_err_t expected) {
  wl_kernel_frame_t out, saved;
  memset(&out, 0xa5, sizeof(out));
  memcpy(&saved, &out, sizeof(out));
  CHECK(wl_kernel_native_none_read(data, length, &out) == expected);
  CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
}

static void test_native_none(void) {
  static uint8_t frame[WL_KERNEL_NATIVE_HEADER + WL_FRAME_MAX_PAYLOAD + 1U];
  for (size_t i = WL_KERNEL_NATIVE_HEADER; i < sizeof(frame); ++i)
    frame[i] = (uint8_t)i;
  /* Preserve every payload byte: the connector writes only the base header. */
  const size_t lengths[] = {0U, 1U, 20U, 32U, 120U, 512U, 1032U, 2048U};
  for (size_t i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
    CHECK(wl_kernel_native_none_seal(frame, sizeof(frame), 0x1234U, lengths[i]) == WL_OK);
    const uint8_t golden[] = {0xa4U, 0U, 0x12U, 0x34U};
    CHECK(memcmp(frame, golden, sizeof(golden)) == 0);
    wl_kernel_frame_t parsed;
    CHECK(wl_kernel_native_none_read(frame, 4U + lengths[i], &parsed) == WL_OK);
    CHECK(parsed.message_id == 0x1234U && parsed.length == lengths[i]);
    CHECK(parsed.payload == frame + 4U);
    for (size_t at = 4U; at < sizeof(frame); ++at) CHECK(frame[at] == (uint8_t)at);
  }
  uint8_t saved[4];
  memcpy(saved, frame, sizeof(saved));
  CHECK(wl_kernel_native_none_seal(frame, 5U, 1U, 2U) == WL_ERR_BUF_TOO_SMALL);
  CHECK(wl_kernel_native_none_seal(frame, sizeof(frame), 0U, 0U) == WL_ERR_INVALID_ARG);
  CHECK(wl_kernel_native_none_seal(frame, sizeof(frame), 1U, 2049U) == WL_ERR_PAYLOAD_TOO_LONG);
  CHECK(memcmp(frame, saved, sizeof(saved)) == 0);
  for (size_t length = 0U; length < 4U; ++length)
    check_bad_frame(frame, length, WL_ERR_BAD_FRAME);
  check_bad_frame(frame, sizeof(frame), WL_ERR_FRAME_TOO_LONG);
  frame[0] = 0U;
  check_bad_frame(frame, 4U, WL_ERR_BAD_FRAME);
  frame[0] = 0xa8U;
  check_bad_frame(frame, 4U, WL_ERR_PROTOCOL_VERSION);
  frame[0] = 0xa4U; frame[1] = 1U;
  check_bad_frame(frame, 4U, WL_ERR_BAD_FRAME);
  frame[1] = 0U; frame[2] = 0U; frame[3] = 0U;
  check_bad_frame(frame, 4U, WL_ERR_BAD_FRAME);
  for (unsigned kind = 1U; kind <= 3U; ++kind) {
    frame[0] = (uint8_t)(0xa4U | kind);
    check_bad_frame(frame, 16U, WL_ERR_NOT_SUPPORTED);
  }
  CHECK(wl_kernel_native_none_seal(frame, sizeof(frame), 102U, 20U) == WL_OK);
  wl_kernel_frame_t parsed;
  CHECK(wl_kernel_native_none_read(frame, 24U, &parsed) == WL_OK);
}

#ifdef WL_PACKET_ZTEST
ZTEST(static_kernel_wire, rpc_vectors) { test_rpc_vectors(); }
ZTEST(static_kernel_wire, malformed_rpc) { test_malformed_rpc(); }
ZTEST(static_kernel_wire, native_none) { test_native_none(); }
ZTEST_SUITE(static_kernel_wire, NULL, NULL, NULL, NULL, NULL);
#else
int main(void) {
  test_rpc_vectors();
  test_malformed_rpc();
  test_native_none();
  return 0;
}
#endif
