/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/storage/fixed_pool.h"

wl_err_t wl_fixed_pool_init(wl_fixed_pool_t *pool, void *storage, size_t storage_size,
    size_t block_size, size_t alignment, unsigned block_count) {
  size_t stride;
  wl_fixed_pool_t configured;
  if (pool == NULL || storage == NULL || block_size == 0U || block_count == 0U ||
      block_count > 64U || alignment == 0U || (alignment & (alignment - 1U)) != 0U ||
      (uintptr_t)storage % alignment != 0U) return WL_ERR_INVALID_ARG;
  if (block_size > SIZE_MAX - (alignment - 1U)) return WL_ERR_INVALID_ARG;
  stride = (block_size + alignment - 1U) & ~(alignment - 1U);
  if (stride > SIZE_MAX / block_count) return WL_ERR_INVALID_ARG;
  if (storage_size < stride * block_count) return WL_ERR_BUF_TOO_SMALL;
  configured.private_storage = storage;
  configured.private_stride = stride;
  configured.private_alignment = alignment;
  configured.private_used = 0U;
  configured.private_count = block_count;
  *pool = configured;
  return WL_OK;
}

static void *allocate(void *context, size_t size, size_t alignment) {
  wl_fixed_pool_t *pool = context;
  unsigned index;
  if (pool == NULL || pool->private_count == 0U || size == 0U ||
      size > pool->private_stride || alignment == 0U ||
      (alignment & (alignment - 1U)) != 0U ||
      alignment > pool->private_alignment) return NULL;
  for (index = 0U; index < pool->private_count; ++index) {
    const uint64_t bit = UINT64_C(1) << index;
    if ((pool->private_used & bit) == 0U) {
      pool->private_used |= bit;
      return (unsigned char *)pool->private_storage + index * pool->private_stride;
    }
  }
  return NULL;
}

static void deallocate(void *context, void *pointer, size_t size, size_t alignment) {
  wl_fixed_pool_t *pool = context;
  /* Allocator callers must pair an exact live allocation with this pool. */
  const size_t offset = (unsigned char *)pointer - (unsigned char *)pool->private_storage;
  (void)size;
  (void)alignment;
  pool->private_used &= ~(UINT64_C(1) << (offset / pool->private_stride));
}

wl_allocator_t wl_fixed_pool_allocator(wl_fixed_pool_t *pool) {
  wl_allocator_t allocator = {allocate, deallocate, pool};
  return allocator;
}

unsigned wl_fixed_pool_in_use(const wl_fixed_pool_t *pool) {
  uint64_t bits = pool != NULL ? pool->private_used : 0U;
  unsigned count = 0U;
  while (bits != 0U) { bits &= bits - 1U; ++count; }
  return count;
}
