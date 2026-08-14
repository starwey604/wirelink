#ifndef INCLUDE_WIRELINK_BIPBUF_H_
#define INCLUDE_WIRELINK_BIPBUF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wirelink/span.h"

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

/* The buffer does not own mem; it must remain valid until the last use of buf. */
int wl_bb_init(wl_bipbuf_t *buf, uint8_t *mem, size_t size);
/* Returns WL_ERR_NO_SPACE when length cannot fit in either writable region. */
int wl_bb_write(wl_bipbuf_t *buf, const uint8_t *data, size_t length);
/* Return WL_ERR_NO_DATA when the requested range is not readable. */
int wl_bb_read(wl_bipbuf_t *buf, uint8_t *data, size_t length);
int wl_bb_peek(wl_bipbuf_t *buf, uint8_t *data, size_t offset, size_t length);
int wl_bb_discard(wl_bipbuf_t *buf, size_t length);
size_t wl_bb_readable_bytes(const wl_bipbuf_t *buf);
size_t wl_bb_get_space(const wl_bipbuf_t *buf);
bool wl_bb_is_full(const wl_bipbuf_t *buf);
bool wl_bb_is_empty(const wl_bipbuf_t *buf);

/*
 * get_write_buf() exposes free space immediately following the active write
 * region.  get_write_buf_force_wrap() instead exposes the space before region
 * A and must be committed with wl_bb_advance_write_index_wrapped().
 */
wl_span_t wl_bb_get_write_buf(wl_bipbuf_t *buf);
wl_span_t wl_bb_get_write_buf_force_wrap(wl_bipbuf_t *buf);
wl_span_t wl_bb_get_contiguous_read_buf(wl_bipbuf_t *buf);
bool wl_bb_advance_write_index(wl_bipbuf_t *buf, size_t length);
bool wl_bb_advance_write_index_wrapped(wl_bipbuf_t *buf, size_t length);
int wl_bb_reset(wl_bipbuf_t *buf);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // INCLUDE_WIRELINK_BIPBUF_H_
