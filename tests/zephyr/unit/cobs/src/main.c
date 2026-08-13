/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/cobs.h"
#include "wirelink/types.h"

ZTEST(wirelink_cobs_unit, test_encoded_max_size_formula)
{
	zassert_equal(wl_cobs_encoded_max_size(0), 1);
	zassert_equal(wl_cobs_encoded_max_size(1), 2);
	zassert_equal(wl_cobs_encoded_max_size(254), 256);
	zassert_equal(wl_cobs_encoded_max_size(255), 257);
	zassert_equal(wl_cobs_encoded_max_size(256), 258);
}

ZTEST(wirelink_cobs_unit, test_encode_decode_empty_payload)
{
	uint8_t out[4] = {0xAA};
	size_t out_len = 0xFFFF;
	uint8_t dec[4] = {0xBB};
	size_t dec_len = 0xFFFF;

	zassert_ok(wl_cobs_encode((const uint8_t *)"", 0, out, sizeof(out), &out_len));
	zassert_equal(out_len, 1);
	zassert_equal(out[0], 1);

	zassert_ok(wl_cobs_decode(out, out_len, dec, sizeof(dec), &dec_len));
	zassert_equal(dec_len, 0);
}

ZTEST(wirelink_cobs_unit, test_encode_decode_no_zero_data)
{
	uint8_t out[16];
	size_t out_len = 0;
	uint8_t dec[16];
	size_t dec_len = 0;
	const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

	zassert_ok(
		wl_cobs_encode(payload, sizeof(payload), out, sizeof(out), &out_len));
	zassert_equal(out[0], 10);
	zassert_mem_equal(&out[1], payload, sizeof(payload));
	zassert_equal(out_len, 10);

	zassert_ok(wl_cobs_decode(out, out_len, dec, sizeof(dec), &dec_len));
	zassert_equal(dec_len, sizeof(payload));
	zassert_mem_equal(dec, payload, sizeof(payload));
}

ZTEST(wirelink_cobs_unit, test_encode_decode_with_zero_data)
{
	uint8_t out[32];
	size_t out_len = 0;
	uint8_t dec[32];
	size_t dec_len = 0;
	const uint8_t payload[] = {1, 2, 0, 3, 0, 4, 5, 0, 0, 6};

	zassert_ok(
		wl_cobs_encode(payload, sizeof(payload), out, sizeof(out), &out_len));
	zassert_ok(wl_cobs_decode(out, out_len, dec, sizeof(dec), &dec_len));
	zassert_equal(dec_len, sizeof(payload));
	zassert_mem_equal(dec, payload, sizeof(payload));
}

ZTEST(wirelink_cobs_unit, test_decode_roundtrip_random_like)
{
	uint8_t in[] = {0x11, 0x00, 0x22, 0x33, 0x44, 0x00, 0x55, 0x66, 0x77,
	               0x88, 0x00, 0x99};
	uint8_t encoded[64];
	size_t encoded_len = 0;
	uint8_t decoded[64];
	size_t decoded_len = 0;

	zassert_ok(wl_cobs_encode(in, sizeof(in), encoded, sizeof(encoded), &encoded_len));
	zassert_ok(wl_cobs_decode(encoded, encoded_len, decoded, sizeof(decoded),
	                         &decoded_len));
	zassert_equal(decoded_len, sizeof(in));
	zassert_mem_equal(decoded, in, sizeof(in));
}

ZTEST(wirelink_cobs_unit, test_errors_and_boundaries)
{
	uint8_t out[1];
	size_t out_len = 0;

	zassert_equal(wl_cobs_encode((const uint8_t *)"A", 1, out, 1, &out_len),
	              WL_ERR_BUF_TOO_SMALL);
	zassert_equal(wl_cobs_decode((const uint8_t *)"", 0, out, sizeof(out),
	                            &out_len),
	              WL_ERR_COBS_DECODE);

	zassert_equal(
		wl_cobs_decode((const uint8_t[]){0x00}, 1, out, sizeof(out), &out_len),
		WL_ERR_COBS_DECODE);
	zassert_equal(
		wl_cobs_decode((const uint8_t[]){0x02, 0xAA}, 2, NULL, 1, &out_len),
		WL_ERR_INVALID_ARG);
}

ZTEST_SUITE(wirelink_cobs_unit, NULL, NULL, NULL, NULL, NULL);
