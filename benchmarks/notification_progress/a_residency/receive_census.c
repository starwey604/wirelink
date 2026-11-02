/* SPDX-License-Identifier: Apache-2.0 */
/* Offline source-path census. Instrumented frozen generated C, never firmware. */
#include <assert.h>
#include <stdio.h>
#include "instrumented.c"

static void check(const char *name, const uint8_t *input, size_t length,
                  const uint8_t *data, size_t size) {
  census_clear = census_getv = census_read_value = census_find_field = 0U;
  census_skip = census_varint_bytes = 0U;
  stream_chunk_t out;
  memset(&out, 0xa5, sizeof(out));
  assert(stream_chunk_decode(input, length, &out) == WL_CODEC_OK);
  assert(out.has_sequence && out.sequence == 0x01020304U && out.has_data);
  assert(out.data.data == data && out.data.length == size);
  printf("{\"case\":\"%s\",\"clear\":%u,\"getv\":%u,\"read_value\":%u,"
         "\"find_field\":%u,\"skip\":%u,\"varint_bytes\":%u}\n",
         name, census_clear, census_getv, census_read_value, census_find_field,
         census_skip, census_varint_bytes);
}

int main(void) {
  uint8_t canonical[39] = {13U, 1U, 2U, 3U, 4U, 18U, 32U};
  uint8_t reordered[39] = {18U, 32U};
  uint8_t unknown[41];
  uint8_t longer[136] = {13U, 1U, 2U, 3U, 4U, 18U, 128U, 1U};
  for (size_t i = 0U; i < 32U; ++i) canonical[7U + i] = (uint8_t)i;
  memcpy(reordered + 2U, canonical + 7U, 32U);
  memcpy(reordered + 34U, canonical, 5U);
  memcpy(unknown, canonical, sizeof(canonical));
  unknown[39] = 24U;
  unknown[40] = 1U;
  check("canonical32", canonical, sizeof(canonical), canonical + 7U, 32U);
  check("reordered32", reordered, sizeof(reordered), reordered + 2U, 32U);
  check("unknown32", unknown, sizeof(unknown), unknown + 7U, 32U);
  check("canonical128", longer, sizeof(longer), longer + 8U, 128U);
  return 0;
}
