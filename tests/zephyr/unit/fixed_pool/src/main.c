/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/ztest.h>
#include "wirelink/storage/fixed_pool.h"

static _Alignas(64) unsigned char storage[64 * 64];

ZTEST(fixed_pool, test_bounded_reuse_all_bits) {
  wl_fixed_pool_t pool;
  void *blocks[64];
  zassert_ok(wl_fixed_pool_init(&pool, storage, sizeof(storage), 33, 64, 64));
  wl_allocator_t allocator = wl_fixed_pool_allocator(&pool);
  for (unsigned i = 0; i < 64; ++i) {
    blocks[i] = allocator.allocate(allocator.context, 33, 64);
    zassert_equal(blocks[i], storage + i * 64);
    memset(blocks[i], i, 33);
  }
  zassert_equal(wl_fixed_pool_in_use(&pool), 64);
  zassert_is_null(allocator.allocate(allocator.context, 1, 1));
  for (unsigned i = 0; i < 64; i += 2)
    allocator.deallocate(allocator.context, blocks[i], 33, 64);
  for (unsigned i = 0; i < 64; i += 2)
    zassert_equal(allocator.allocate(allocator.context, 33, 64), blocks[i]);
  for (unsigned i = 0; i < 64; ++i)
    allocator.deallocate(allocator.context, blocks[i], 33, 64);
  zassert_equal(wl_fixed_pool_in_use(&pool), 0);
}

ZTEST(fixed_pool, test_invalid_inputs_do_not_modify_pool) {
  wl_fixed_pool_t pool, before;
  memset(&pool, 0xa5, sizeof(pool));
  before = pool;
  zassert_equal(wl_fixed_pool_init(&pool, storage, sizeof(storage), SIZE_MAX, 64, 1), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fixed_pool_init(&pool, storage, SIZE_MAX, SIZE_MAX / 2 + 1, 1, 2), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fixed_pool_init(&pool, storage + 1, sizeof(storage) - 1, 32, 64, 2), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fixed_pool_init(&pool, storage, sizeof(storage), 32, 3, 2), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fixed_pool_init(&pool, storage, sizeof(storage), 32, 0, 2), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fixed_pool_init(&pool, storage, 63, 32, 64, 2), WL_ERR_BUF_TOO_SMALL);
  zassert_equal(wl_fixed_pool_init(&pool, storage, sizeof(storage), 1, 1, 65), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fixed_pool_init(&pool, storage, sizeof(storage), 0, 1, 1), WL_ERR_INVALID_ARG);
  zassert_mem_equal(&pool, &before, sizeof(pool));
}

ZTEST(fixed_pool, test_request_limits_and_pool_isolation) {
  wl_fixed_pool_t a, b;
  zassert_ok(wl_fixed_pool_init(&a, storage, 64, 33, 64, 1));
  zassert_ok(wl_fixed_pool_init(&b, storage + 64, 64, 33, 64, 1));
  wl_allocator_t allocator = wl_fixed_pool_allocator(&a);
  zassert_is_null(allocator.allocate(allocator.context, 65, 1));
  zassert_is_null(allocator.allocate(allocator.context, 1, 128));
  zassert_is_null(allocator.allocate(allocator.context, 1, 3));
  zassert_is_null(allocator.allocate(allocator.context, 0, 1));
  zassert_equal(wl_fixed_pool_in_use(&a), 0);
  zassert_not_null(allocator.allocate(allocator.context, 33, 64));
  zassert_equal(wl_fixed_pool_in_use(&a), 1);
  zassert_equal(wl_fixed_pool_in_use(&b), 0);
}

ZTEST_SUITE(fixed_pool, NULL, NULL, NULL, NULL, NULL);
