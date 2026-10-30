/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_DIAGNOSTICS_H_
#define WIRELINK_DIAGNOSTICS_H_
#include "wirelink/kernel.h"
#include "wirelink/fifo.h"
#include "wirelink/latest.h"
#include "wirelink/outbox.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit, allocation-free key/value formatting. Always NUL-terminate a
 * nonempty buffer; required counts all bytes before truncation, excluding NUL.
 * These functions never query/advance the endpoint or perform hot-path logging. */
typedef struct {
  char *data;
  size_t capacity, length, required;
} wl_diag_writer_t;
wl_err_t wl_diag_writer_init(wl_diag_writer_t *, char *, size_t);
void wl_diag_writer_reset(wl_diag_writer_t *);
wl_err_t wl_diag_format_fifo_stats(wl_diag_writer_t *, const wl_fifo_stats_t *);
wl_err_t wl_diag_format_latest_stats(wl_diag_writer_t *, const wl_latest_stats_t *);
wl_err_t wl_diag_format_outbox_stats(wl_diag_writer_t *, const wl_outbox_stats_t *);
wl_err_t wl_diag_format_kernel_result(wl_diag_writer_t *, const wl_kernel_result_t *);
#ifdef __cplusplus
}
#endif
#endif
