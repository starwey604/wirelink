#include "wirelink/bipbuf.h"
#include "wirelink/types.h"

#include <string.h>

static bool is_initialized(const wl_bipbuf_t *buf) {
  return buf != NULL && buf->__private.data != NULL && buf->capacity != 0;
}

static int initialization_error(const wl_bipbuf_t *buf) {
  return buf == NULL ? WL_ERR_INVALID_ARG : WL_ERR_NOT_INITIALIZED;
}

static wl_span_t empty_span(void) {
  return (wl_span_t){.data = NULL, .length = 0};
}

int wl_bb_init(wl_bipbuf_t *buf, uint8_t *mem, size_t size) {
  if (buf == NULL || mem == NULL || size == 0) {
    return WL_ERR_INVALID_ARG;
  }

  buf->capacity = size;
  buf->__private.data = mem;
  return wl_bb_reset(buf);
}

int wl_bb_write(wl_bipbuf_t *buf, const uint8_t *data, size_t length) {
  if (!is_initialized(buf)) {
    return initialization_error(buf);
  }
  if (length != 0 && data == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (length == 0) {
    return WL_OK;
  }

  wl_span_t span = wl_bb_get_write_buf(buf);
  if (length <= span.length) {
    memcpy(span.data, data, length);
    return wl_bb_advance_write_index(buf, length) ? WL_OK
                                                  : WL_ERR_INVALID_STATE;
  }

  span = wl_bb_get_write_buf_force_wrap(buf);
  if (length <= span.length) {
    memcpy(span.data, data, length);
    return wl_bb_advance_write_index_wrapped(buf, length)
               ? WL_OK
               : WL_ERR_INVALID_STATE;
  }

  return WL_ERR_NO_SPACE;
}

int wl_bb_peek(wl_bipbuf_t *buf, uint8_t *data, size_t offset, size_t length) {
  if (!is_initialized(buf)) {
    return initialization_error(buf);
  }
  if (length != 0 && data == NULL) {
    return WL_ERR_INVALID_ARG;
  }

  size_t readable = wl_bb_readable_bytes(buf);
  if (offset > readable || length > readable - offset) {
    return WL_ERR_NO_DATA;
  }
  if (length == 0) {
    return WL_OK;
  }

  if (offset < buf->__private.size_a) {
    size_t from_a = buf->__private.size_a - offset;
    if (from_a > length) {
      from_a = length;
    }
    memcpy(data, buf->__private.data + buf->__private.head_a + offset, from_a);
    if (from_a != length) {
      memcpy(data + from_a, buf->__private.data, length - from_a);
    }
  } else {
    memcpy(data, buf->__private.data + (offset - buf->__private.size_a),
           length);
  }
  return WL_OK;
}

int wl_bb_read(wl_bipbuf_t *buf, uint8_t *data, size_t length) {
  int result = wl_bb_peek(buf, data, 0, length);
  return result == WL_OK ? wl_bb_discard(buf, length) : result;
}

int wl_bb_discard(wl_bipbuf_t *buf, size_t length) {
  if (!is_initialized(buf)) {
    return initialization_error(buf);
  }

  size_t readable = wl_bb_readable_bytes(buf);
  if (length > readable) {
    return WL_ERR_NO_DATA;
  }
  if (length == 0) {
    return WL_OK;
  }
  if (length == readable) {
    return wl_bb_reset(buf);
  }

  if (length < buf->__private.size_a) {
    buf->__private.head_a += length;
    buf->__private.size_a -= length;
    return WL_OK;
  }

  /* Region A is exhausted.  Region B becomes the next contiguous region. */
  size_t consumed_from_b = length - buf->__private.size_a;
  buf->__private.head_a = consumed_from_b;
  buf->__private.size_a = buf->__private.size_b - consumed_from_b;
  buf->__private.size_b = 0;
  return WL_OK;
}

size_t wl_bb_readable_bytes(const wl_bipbuf_t *buf) {
  return is_initialized(buf) ? buf->__private.size_a + buf->__private.size_b
                             : 0;
}

size_t wl_bb_get_space(const wl_bipbuf_t *buf) {
  return is_initialized(buf) ? buf->capacity - wl_bb_readable_bytes(buf) : 0;
}

bool wl_bb_is_full(const wl_bipbuf_t *buf) { return wl_bb_get_space(buf) == 0; }

bool wl_bb_is_empty(const wl_bipbuf_t *buf) {
  return wl_bb_readable_bytes(buf) == 0;
}

wl_span_t wl_bb_get_write_buf(wl_bipbuf_t *buf) {
  if (!is_initialized(buf)) {
    return empty_span();
  }
  if (buf->__private.size_b != 0) {
    return (wl_span_t){.data = buf->__private.data + buf->__private.size_b,
                       .length = buf->__private.head_a - buf->__private.size_b};
  }

  size_t tail = buf->__private.head_a + buf->__private.size_a;
  return (wl_span_t){.data = buf->__private.data + tail,
                     .length = buf->capacity - tail};
}

wl_span_t wl_bb_get_write_buf_force_wrap(wl_bipbuf_t *buf) {
  if (!is_initialized(buf) || buf->__private.size_b != 0) {
    return empty_span();
  }
  return (wl_span_t){.data = buf->__private.data,
                     .length = buf->__private.head_a};
}

wl_span_t wl_bb_get_contiguous_read_buf(wl_bipbuf_t *buf) {
  if (!is_initialized(buf) || buf->__private.size_a == 0) {
    return empty_span();
  }
  return (wl_span_t){.data = buf->__private.data + buf->__private.head_a,
                     .length = buf->__private.size_a};
}

bool wl_bb_advance_write_index(wl_bipbuf_t *buf, size_t length) {
  if (!is_initialized(buf) || length > wl_bb_get_write_buf(buf).length) {
    return false;
  }
  if (buf->__private.size_b != 0) {
    buf->__private.size_b += length;
  } else {
    buf->__private.size_a += length;
  }
  return true;
}

bool wl_bb_advance_write_index_wrapped(wl_bipbuf_t *buf, size_t length) {
  if (!is_initialized(buf) || buf->__private.size_b != 0 ||
      length > buf->__private.head_a) {
    return false;
  }
  buf->__private.size_b = length;
  return true;
}

int wl_bb_reset(wl_bipbuf_t *buf) {
  if (!is_initialized(buf)) {
    return initialization_error(buf);
  }
  buf->__private.head_a = 0;
  buf->__private.size_a = 0;
  buf->__private.size_b = 0;
  return WL_OK;
}
