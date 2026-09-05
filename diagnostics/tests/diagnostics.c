/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/diagnostics.h"

#include <stdint.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (0)

int main(void) {
  char output[512];
  char truncated[12];
  wl_diag_writer_t writer;
  wl_diag_writer_t sizing;
  const wl_rx_counters_t rx = {
      .malformed = 1U,
      .bad_integrity = 2U,
      .overflow = 3U,
      .duplicate = 4U,
      .unsupported = UINT32_MAX,
  };
  const wl_rpc_client_result_t rpc = {
      .operation_id = 42U,
      .request_message_id = 10U,
      .response_message_id = 11U,
      .state = WL_RPC_CLIENT_APPLICATION_ERROR,
      .tx_handle = 7U,
      .link_result = -38,
      .application_status = INT32_MIN,
      .runtime_error = WL_RPC_ERR_RESPONSE_MISMATCH,
      .link_delivery_confirmed = 1U,
      .response_length = 123U,
  };

  CHECK(wl_diag_writer_init(&writer, output, sizeof(output)) == WL_OK);
  CHECK(wl_diag_format_rx_counters(&writer, &rx) == WL_OK);
  CHECK(strcmp(output, "rx malformed=1 bad_integrity=2 overflow=3 duplicate=4 "
                       "unsupported=4294967295\n") == 0);
  CHECK(wl_diag_format_rpc_client_result(&writer, &rpc) == WL_OK);
  CHECK(strstr(output, " application_status=-2147483648 ") != NULL);
  CHECK(writer.length == strlen(output));
  CHECK(writer.required == writer.length);

  CHECK(wl_diag_writer_init(&sizing, NULL, 0U) == WL_OK);
  CHECK(wl_diag_format_rx_counters(&sizing, &rx) == WL_ERR_BUF_TOO_SMALL);
  CHECK(sizing.length == 0U);
  CHECK(sizing.required + 1U > sizeof(truncated));

  CHECK(wl_diag_writer_init(&writer, truncated, sizeof(truncated)) == WL_OK);
  CHECK(wl_diag_format_rx_counters(&writer, &rx) == WL_ERR_BUF_TOO_SMALL);
  CHECK(writer.length == sizeof(truncated) - 1U);
  CHECK(truncated[sizeof(truncated) - 1U] == '\0');
  CHECK(writer.required == sizing.required);
  wl_diag_writer_reset(&writer);
  CHECK(writer.length == 0U && writer.required == 0U && truncated[0] == '\0');

  CHECK(wl_diag_writer_init(NULL, output, sizeof(output)) ==
        WL_ERR_INVALID_ARG);
  CHECK(wl_diag_writer_init(&writer, NULL, 1U) == WL_ERR_INVALID_ARG);
  CHECK(wl_diag_writer_init(&writer, output, sizeof(output)) == WL_OK);
  CHECK(wl_diag_format_fifo_stats(&writer, NULL) == WL_ERR_INVALID_ARG);
  return 0;
}
