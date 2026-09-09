/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <string.h>
static void varint(uint8_t *out, size_t *n, uint64_t v) {
  while (v >= 128) { out[(*n)++] = (uint8_t)(v | 128U); v >>= 7; }
  out[(*n)++] = (uint8_t)v;
}
static void field(codec_plan_fixture_t *f, unsigned i, uint8_t *out, size_t *n) {
  unsigned number = f->kind < 5 ? i + 1 : 1 + i * 997;
  varint(out, n, (uint64_t)number << 3);
  varint(out, n, 128U + i * 17U);
}
#define CASE(name, index) case index: \
  if (f->stage == 0) return name##_decode(f->input, f->input_length, &f->view.name); \
  if (f->stage == 1) return name##_encode(&f->view.name, f->output, sizeof(f->output), &f->output_length); \
  f->output_length = name##_encoded_size(&f->view.name); \
  return f->output_length == SIZE_MAX ? WL_CODEC_ERR_INVALID_VALUE : WL_CODEC_OK;
int codec_plan_step(codec_plan_fixture_t *f) {
  switch (f->kind) {
    CASE(dense2, 0) CASE(dense8, 1) CASE(dense16, 2) CASE(dense32, 3) CASE(dense64, 4)
    CASE(sparse8, 5) CASE(sparse16, 6) CASE(sparse32, 7) CASE(sparse64, 8)
    CASE(packed30, 9) CASE(packed128, 10) CASE(packed64, 11)
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
int codec_plan_init(codec_plan_fixture_t *f, unsigned kind, unsigned pattern, unsigned stage) {
  if (kind >= CODEC_PLAN_KINDS || pattern >= CODEC_PLAN_PATTERNS || stage >= CODEC_PLAN_STAGES)
    return WL_CODEC_ERR_INVALID_VALUE;
  memset(f, 0, sizeof(*f)); f->kind = kind; f->pattern = pattern;
  if (kind < 9) {
    static const unsigned counts[] = {2, 8, 16, 32, 64, 8, 16, 32, 64};
    unsigned count = counts[kind];
    for (unsigned i = 0; i < count; ++i) {
      if (pattern != 2 || i == count - 1) field(f, i, f->expected, &f->expected_length);
      unsigned input_index = pattern == 1 ? count - 1 - i : i;
      if (pattern != 2 || input_index == count - 1) field(f, input_index, f->input, &f->input_length);
    }
  } else {
    unsigned count = kind == 9 ? 30 : kind == 10 ? 128 : 64;
    unsigned width = kind == 11 ? 8 : 4;
    unsigned number = kind == 9 ? 1 : kind == 10 ? 21 : 65535;
    varint(f->expected, &f->expected_length, ((uint64_t)number << 3) | 2);
    varint(f->expected, &f->expected_length, count * width);
    for (unsigned i = 0; i < count; ++i) {
      uint64_t bits;
      if (width == 4) { float value = (float)i - 0.5F; uint32_t v; memcpy(&v, &value, 4); bits = v; }
      else { double value = (double)i - 0.5; memcpy(&bits, &value, 8); }
      for (unsigned b = width; b; --b) f->expected[f->expected_length++] = (uint8_t)(bits >> ((b - 1) * 8));
    }
    memcpy(f->input, f->expected, f->expected_length); f->input_length = f->expected_length;
  }
  /* Last-field-only tests sparse lookup; packed messages keep the full array.
   * Pattern 2 also includes unknown fields before and after the known content. */
  if (pattern == 2) {
    uint8_t unknown[8]; size_t n = 0;
    varint(unknown, &n, UINT64_C(65000) << 3); varint(unknown, &n, 42);
    memmove(f->input + n, f->input, f->input_length);
    memcpy(f->input, unknown, n); f->input_length += n;
    memcpy(f->input + f->input_length, unknown, n); f->input_length += n;
  }
  int result = codec_plan_step(f); f->stage = stage;
  return result;
}
int codec_plan_check(codec_plan_fixture_t *f) {
  unsigned stage = f->stage;
  f->stage = 1;
  int result = codec_plan_step(f);
  f->stage = stage;
  if (result != WL_CODEC_OK || f->output_length != f->expected_length ||
      memcmp(f->output, f->expected, f->expected_length) != 0) return WL_CODEC_ERR_INVALID_VALUE;
  return WL_CODEC_OK;
}
