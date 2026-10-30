/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/diagnostics.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int main(void) {
  const wl_kernel_result_t result = {WL_KERNEL_REJECTED, INT32_MIN};
  const char *expected = "kernel_result outcome=1 status=-2147483648\n";
  char output[512];
  wl_diag_writer_t writer;
  for (size_t capacity = 0; capacity <= strlen(expected) + 1; ++capacity) {
    memset(output, 'x', sizeof(output));
    CHECK(wl_diag_writer_init(&writer, capacity ? output : NULL, capacity) == WL_OK);
    CHECK(wl_diag_format_kernel_result(&writer, &result) ==
        (capacity > strlen(expected) ? WL_OK : WL_ERR_BUF_TOO_SMALL));
    CHECK(writer.required == strlen(expected));
    CHECK(writer.length == (capacity == 0 ? 0 : capacity - 1));
    if (capacity) {
      CHECK(output[writer.length] == '\0');
      CHECK(memcmp(output, expected, writer.length) == 0);
    }
    CHECK(output[capacity] == 'x');
  }
  CHECK(wl_diag_writer_init(&writer, output, sizeof(output)) == WL_OK);
  const wl_outbox_stats_t outbox = {.submitted = UINT64_MAX};
  CHECK(wl_diag_format_outbox_stats(&writer, &outbox) == WL_OK);
  CHECK(strstr(output, "submitted=18446744073709551615") != NULL);
  wl_diag_writer_reset(&writer);
  const wl_fifo_stats_t fifo = {.depth = 3, .errors = UINT32_MAX};
  CHECK(wl_diag_format_fifo_stats(&writer, &fifo) == WL_OK);
  CHECK(strncmp(output, "fifo depth=3 ", 13) == 0);
  CHECK(strstr(output, "errors=4294967295\n") != NULL);
  wl_diag_writer_reset(&writer);
  const wl_latest_stats_t latest = {.generation = UINT32_MAX};
  CHECK(wl_diag_format_latest_stats(&writer, &latest) == WL_OK);
  CHECK(strstr(output, "generation=4294967295") != NULL);
  CHECK(wl_diag_writer_init(NULL, output, sizeof(output)) == WL_ERR_INVALID_ARG);
  CHECK(wl_diag_writer_init(&writer, NULL, 1) == WL_ERR_INVALID_ARG);
  CHECK(wl_diag_format_kernel_result(&writer, NULL) == WL_ERR_INVALID_ARG);
  return 0;
}
