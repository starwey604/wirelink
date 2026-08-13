/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/bipbuf.h"
#include "wirelink/types.h"

ZTEST(wirelink_bipbuf, test_init_and_error_semantics)
{
	uint8_t storage[4];
	uint8_t output[4];
	wl_bipbuf_t buffer = {0};

	zassert_equal(wl_bb_init(NULL, storage, sizeof(storage)), WL_ERR_INVALID_ARG);
	zassert_equal(wl_bb_init(&buffer, NULL, sizeof(storage)), WL_ERR_INVALID_ARG);
	zassert_equal(wl_bb_init(&buffer, storage, 0), WL_ERR_INVALID_ARG);
	zassert_equal(wl_bb_write(&buffer, storage, 1), WL_ERR_NOT_INITIALIZED);

	zassert_ok(wl_bb_init(&buffer, storage, sizeof(storage)));
	zassert_equal(wl_bb_read(&buffer, output, 1), WL_ERR_NO_DATA);
	zassert_equal(wl_bb_discard(&buffer, 1), WL_ERR_NO_DATA);
	zassert_equal(wl_bb_write(&buffer, (const uint8_t *)"abcd", 4), WL_OK);
	zassert_equal(wl_bb_write(&buffer, (const uint8_t *)"e", 1), WL_ERR_NO_SPACE);
}

ZTEST(wirelink_bipbuf, test_write_read_across_wrapped_regions)
{
	uint8_t storage[8];
	uint8_t output[8];
	wl_bipbuf_t buffer;

	zassert_ok(wl_bb_init(&buffer, storage, sizeof(storage)));
	zassert_ok(wl_bb_write(&buffer, (const uint8_t *)"abcdef", 6));
	zassert_ok(wl_bb_discard(&buffer, 4));
	zassert_ok(wl_bb_write(&buffer, (const uint8_t *)"WXYZ", 4));

	zassert_equal(wl_bb_readable_bytes(&buffer), 6);
	zassert_ok(wl_bb_peek(&buffer, output, 1, 5));
	zassert_mem_equal(output, "fWXYZ", 5);
	zassert_ok(wl_bb_read(&buffer, output, 6));
	zassert_mem_equal(output, "efWXYZ", 6);
	zassert_true(wl_bb_is_empty(&buffer));
}

ZTEST(wirelink_bipbuf, test_zero_copy_wrapped_write_and_region_promotion)
{
	uint8_t storage[8];
	wl_bipbuf_t buffer;
	wl_span_t span;

	zassert_ok(wl_bb_init(&buffer, storage, sizeof(storage)));
	zassert_ok(wl_bb_write(&buffer, (const uint8_t *)"abcde", 5));
	zassert_ok(wl_bb_discard(&buffer, 3));

	span = wl_bb_get_write_buf_force_wrap(&buffer);
	zassert_equal(span.length, 3);
	memcpy(span.data, "XYZ", 3);
	zassert_true(wl_bb_advance_write_index_wrapped(&buffer, 3));

	span = wl_bb_get_contiguous_read_buf(&buffer);
	zassert_equal(span.length, 2);
	zassert_mem_equal(span.data, "de", 2);
	zassert_ok(wl_bb_discard(&buffer, 2));

	span = wl_bb_get_contiguous_read_buf(&buffer);
	zassert_equal(span.length, 3);
	zassert_mem_equal(span.data, "XYZ", 3);
}

ZTEST_SUITE(wirelink_bipbuf, NULL, NULL, NULL, NULL, NULL);
