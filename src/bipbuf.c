#include "wirelink/bipbuf.h"
#include "wirelink/span.h"
#include "wirelink/types.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline bool _in_region_a(wl_bipbuf_t *buf, size_t offset);

int wl_bb_init(wl_bipbuf_t *buf, uint8_t *mem, size_t size) {
  if (buf == NULL || mem == NULL || (size & (size - 1)) == 0) {
    return WL_ERR_INVALID_ARG;
  }
  buf->__private.data = mem;
  buf->capacity = size;
  _wl_bb_reset(buf);
  return WL_OK;
}

bool wl_bb_write(wl_bipbuf_t *buf, const uint8_t *data, size_t length) {
  if (length == 0) {
    return true;
  }

  wl_span_t span = wl_bb_get_write_buf(buf);

  // try writing current region (A or B)
  if (span.length >= length) {
    memcpy(span.data, data, length);
    return wl_bb_advance_write_index(buf, length);
  }

  // if not, and we're trying writing region A,
  if (buf->__private.size_b == 0) {
    // check if we can write on region B
    if (buf->__private.head_a >= length) {
      memcpy(buf, data, length);
      buf->__private.size_b = length;
      return true;
    }
  }

  return false;
}

bool wl_bb_peek(wl_bipbuf_t *buf, uint8_t *data, size_t offset, size_t length) {
  if (offset + length > wl_bb_available(buf)) {
    return false;
  }

  if (_in_region_a(buf, offset)) {
    size_t read_from_a = min(length, buf->__private.size_a - offset);
  }
}

void wl_bb_reset(wl_bipbuf_t *buf) {
  assert(buf != NULL);
  buf->__private.head_a = 0;
  buf->__private.size_a = 0;
  buf->__private.size_b = 0;
}

static inline bool _in_region_a(wl_bipbuf_t *buf, size_t offset) {
  return offset < buf->__private.size_a;
}
