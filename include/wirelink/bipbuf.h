#ifndef INCLUDE_WIRELINK_BIPBUF_H_
#define INCLUDE_WIRELINK_BIPBUF_H_

#include <stdbool.h>
#include <stddef.h>

#include "span.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wl_bipbuf {
  size_t capacity;

  struct {
    uint8_t *data;
    size_t head_a;
    size_t size_a;
    size_t size_b;
  } __private;

} wl_bipbuf_t;

int wl_bb_init(wl_bipbuf_t *buf, uint8_t *mem, size_t size);
bool wl_bb_write(wl_bipbuf_t *buf, const uint8_t *data, size_t length);
bool wl_bb_read(wl_bipbuf_t *buf, uint8_t *data, size_t length);
bool wl_bb_peek(wl_bipbuf_t *buf, uint8_t *data, size_t offset, size_t length);
bool wl_bb_discard(wl_bipbuf_t *buf, size_t length);
size_t wl_bb_available(wl_bipbuf_t *buf);
size_t wl_bb_space(wl_bipbuf_t *buf);
bool wl_bb_is_full(wl_bipbuf_t *buf);
bool wl_bb_is_empty(wl_bipbuf_t *buf);
wl_span_t wl_bb_get_write_buf(wl_bipbuf_t *buf);
wl_span_t wl_bb_get_write_buf_force_wrap(wl_bipbuf_t *buf);
wl_span_t wl_bb_get_contiguous_read_buf(wl_bipbuf_t *buf);
bool wl_bb_advance_write_index(wl_bipbuf_t *buf, size_t length);
bool wl_bb_advance_write_index_wrapped(wl_bipbuf_t *buf, size_t length);
void wl_bb_reset(wl_bipbuf_t *buf);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // INCLUDE_WIRELINK_BIPBUF_H_
