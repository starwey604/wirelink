/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_STORAGE_FIXED_POOL_H
#define WIRELINK_STORAGE_FIXED_POOL_H

#include <stdint.h>
#include "wirelink/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional single-owner fixed-block allocator. Metadata never occupies a live
 * endpoint's storage; no heap, realloc, locks or global allocator. At most 64
 * blocks per pool. Init only before use; all allocations must be destroyed
 * before pool/storage reuse. Members are private. */
typedef struct {
  void *private_storage;
  size_t private_stride;
  size_t private_alignment;
  uint64_t private_used;
  unsigned private_count;
} wl_fixed_pool_t;

/* Exact block count; stride is block_size rounded up to alignment. Rejects
 * invalid alignment, overflow or insufficient storage without changing pool.
 * The supplied storage address must already satisfy alignment. */
wl_err_t wl_fixed_pool_init(wl_fixed_pool_t *pool, void *storage, size_t storage_size,
    size_t block_size, size_t alignment, unsigned block_count);
wl_allocator_t wl_fixed_pool_allocator(wl_fixed_pool_t *pool);
unsigned wl_fixed_pool_in_use(const wl_fixed_pool_t *pool);

#ifdef __cplusplus
}
#endif
#endif
