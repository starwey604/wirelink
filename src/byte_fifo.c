/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/byte_fifo.h"
#include <stdbool.h>
#include <string.h>
static uint32_t distance(const wl_byte_fifo_t *f, uint32_t write, uint32_t read) {
  return write >= read ? write - read : (uint32_t)(2U * f->storage.length) - read + write;
}
static uint32_t advance(const wl_byte_fifo_t *f, uint32_t cursor, size_t length) {
  uint32_t limit = (uint32_t)(2U * f->storage.length);
  uint32_t remaining = limit - cursor;
  return length >= remaining ? (uint32_t)length - remaining : cursor + (uint32_t)length;
}
wl_err_t wl_byte_fifo_init(wl_byte_fifo_t *f, wl_span_t storage,
                           wl_packet_notify_fn notify, void *user) {
  if (f == NULL || storage.data == NULL || storage.length == 0U ||
      storage.length > UINT32_MAX / 2U) return WL_ERR_INVALID_ARG;
  memset(f, 0, sizeof(*f)); f->storage = storage; f->notify = notify; f->user = user;
  atomic_init(&f->read, 0U); atomic_init(&f->write, 0U); atomic_init(&f->gap, false);
  return atomic_is_lock_free(&f->read) && atomic_is_lock_free(&f->write) && atomic_is_lock_free(&f->gap)
      ? WL_OK : WL_ERR_NOT_SUPPORTED;
}
wl_err_t wl_byte_fifo_reserve(wl_byte_fifo_t *f, wl_span_t *out) {
  if (f == NULL || out == NULL) return WL_ERR_INVALID_ARG;
  *out = (wl_span_t){0};
  if (f->reservation) return WL_ERR_BUSY;
  uint32_t write = atomic_load_explicit(&f->write, memory_order_relaxed);
  uint32_t read = atomic_load_explicit(&f->read, memory_order_acquire);
  size_t free = f->storage.length - distance(f, write, read);
  if (free == 0U) return WL_ERR_WOULD_BLOCK;
  size_t at = write % f->storage.length, n = f->storage.length - at;
  if (n > free) n = free;
  f->reserved = n; f->reservation = 1U;
  *out = (wl_span_t){f->storage.data + at, n};
  return WL_OK;
}
wl_err_t wl_byte_fifo_commit(wl_byte_fifo_t *f, size_t n) {
  if (f == NULL || !f->reservation || n > f->reserved) return WL_ERR_INVALID_ARG;
  uint32_t write = atomic_load_explicit(&f->write, memory_order_relaxed);
  f->reservation = 0U; f->reserved = 0U;
  atomic_store_explicit(&f->write, advance(f, write, n), memory_order_release);
  if (n != 0U && f->notify) f->notify(f->user);
  return WL_OK;
}
int wl_byte_fifo_has_space(const wl_byte_fifo_t *f) {
  uint32_t read = atomic_load_explicit(&f->read, memory_order_acquire);
  uint32_t write = atomic_load_explicit(&f->write, memory_order_acquire);
  return distance(f, write, read) < f->storage.length;
}
wl_span_t wl_byte_fifo_peek(wl_byte_fifo_t *f) {
  uint32_t read = atomic_load_explicit(&f->read, memory_order_relaxed);
  uint32_t write = atomic_load_explicit(&f->write, memory_order_acquire);
  size_t at = read % f->storage.length;
  size_t n = distance(f, write, read), limit = f->storage.length - at;
  if (n > limit) n = limit;
  f->peeked = n;
  return (wl_span_t){f->storage.data + at, n};
}
wl_err_t wl_byte_fifo_consume(wl_byte_fifo_t *f, size_t n) {
  if (n > f->peeked) return WL_ERR_INVALID_ARG;
  uint32_t read = atomic_load_explicit(&f->read, memory_order_relaxed);
  f->peeked = 0U;
  atomic_store_explicit(&f->read, advance(f, read, n), memory_order_release);
  return WL_OK;
}
int wl_byte_fifo_readable(const wl_byte_fifo_t *f) {
  uint32_t read = atomic_load_explicit(&f->read, memory_order_relaxed);
  return atomic_load_explicit(&f->write, memory_order_acquire) != read;
}
void wl_byte_fifo_note_gap(wl_byte_fifo_t *f) {
  atomic_store_explicit(&f->gap, true, memory_order_release);
  if (f->notify) f->notify(f->user);
}
int wl_byte_fifo_take_gap(wl_byte_fifo_t *f) {
  return atomic_exchange_explicit(&f->gap, false, memory_order_acq_rel);
}
