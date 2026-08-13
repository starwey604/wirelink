/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/cobs.h"

ZTEST(wirelink_cobs_integration, test_integration_roundtrip)
{
	uint8_t payload[16] = {1, 2, 0, 0, 3, 4, 5, 0, 6, 7, 8, 9, 10, 11, 0, 12};
	uint8_t encoded[32];
	size_t encoded_len = 0;
	uint8_t decoded[32];
	size_t decoded_len = 0;

	zassert_ok(wl_cobs_encode(payload, sizeof(payload), encoded, sizeof(encoded),
	                          &encoded_len));
	zassert_true(encoded_len > 0);
	zassert_ok(wl_cobs_decode(encoded, encoded_len, decoded, sizeof(decoded),
	                          &decoded_len));
	zassert_equal(decoded_len, sizeof(payload));
	zassert_mem_equal(decoded, payload, sizeof(payload));
}

ZTEST(wirelink_cobs_integration, test_streaming_style_chunks)
{
	const uint8_t payload[] = {0x10, 0x11, 0x00, 0x12, 0x13, 0x14, 0x00, 0x15};
	uint8_t encoded[64];
	size_t encoded_len = 0;
	uint8_t decoded[64];
	size_t decoded_len = 0;

	zassert_ok(wl_cobs_encode(payload, sizeof(payload), encoded, sizeof(encoded),
	                          &encoded_len));
	zassert_equal(wl_cobs_decode(encoded, 2, decoded, sizeof(decoded), &decoded_len),
	              WL_ERR_COBS_DECODE);
	zassert_equal(decoded_len, 0);
	zassert_equal(wl_cobs_decode(encoded, encoded_len, decoded, 4, &decoded_len),
	              WL_ERR_BUF_TOO_SMALL);
	zassert_ok(
		wl_cobs_decode(encoded, encoded_len, decoded, sizeof(decoded), &decoded_len));
	zassert_equal(decoded_len, sizeof(payload));
	zassert_mem_equal(decoded, payload, sizeof(payload));
}

ZTEST_SUITE(wirelink_cobs_integration, NULL, NULL, NULL, NULL, NULL);
