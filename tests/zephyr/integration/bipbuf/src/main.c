/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/bipbuf.h"
#include "wirelink/types.h"

ZTEST(wirelink_bipbuf_integration, test_wrapped_data_flow)
{
	uint8_t storage[8];
	uint8_t output[8];
	wl_bipbuf_t buffer;

	zassert_ok(wl_bb_init(&buffer, storage, sizeof(storage)));
	zassert_ok(wl_bb_write(&buffer, (const uint8_t *)"abcdef", 6));
	zassert_ok(wl_bb_discard(&buffer, 4));
	zassert_ok(wl_bb_write(&buffer, (const uint8_t *)"WXYZ", 4));

	zassert_equal(wl_bb_readable_bytes(&buffer), 6);
	zassert_equal(wl_bb_get_space(&buffer), 2);
	zassert_ok(wl_bb_read(&buffer, output, 6));
	zassert_mem_equal(output, "efWXYZ", 6);
	zassert_true(wl_bb_is_empty(&buffer));
}

ZTEST(wirelink_bipbuf_integration, test_zero_copy_write_flow)
{
	uint8_t storage[8];
	wl_bipbuf_t buffer;
	wl_span_t write_span;
	wl_span_t read_span;

	zassert_ok(wl_bb_init(&buffer, storage, sizeof(storage)));
	zassert_ok(wl_bb_write(&buffer, (const uint8_t *)"abcde", 5));
	zassert_ok(wl_bb_discard(&buffer, 3));

	write_span = wl_bb_get_write_buf_force_wrap(&buffer);
	zassert_equal(write_span.length, 3);
	memcpy(write_span.data, "XYZ", 3);
	zassert_true(wl_bb_advance_write_index_wrapped(&buffer, 3));

	read_span = wl_bb_get_contiguous_read_buf(&buffer);
	zassert_equal(read_span.length, 2);
	zassert_mem_equal(read_span.data, "de", 2);
	zassert_ok(wl_bb_discard(&buffer, 2));

	read_span = wl_bb_get_contiguous_read_buf(&buffer);
	zassert_equal(read_span.length, 3);
	zassert_mem_equal(read_span.data, "XYZ", 3);
}

ZTEST_SUITE(wirelink_bipbuf_integration, NULL, NULL, NULL, NULL, NULL);
