/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CODEC_PLAN_WORKLOAD_H
#define CODEC_PLAN_WORKLOAD_H
#include "plan.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CODEC_PLAN_KINDS 12U
#define CODEC_PLAN_PATTERNS 3U
#define CODEC_PLAN_STAGES 3U
typedef union {
  dense2_t dense2; dense8_t dense8; dense16_t dense16;
  dense32_t dense32; dense64_t dense64;
  sparse8_t sparse8; sparse16_t sparse16; sparse32_t sparse32; sparse64_t sparse64;
  packed30_t packed30; packed128_t packed128; packed64_t packed64;
} codec_plan_view_t;
typedef struct {
  codec_plan_view_t view;
  uint8_t input[2048], expected[2048], output[2048];
  size_t input_length, expected_length, output_length;
  unsigned kind, pattern, stage;
} codec_plan_fixture_t;
int codec_plan_init(codec_plan_fixture_t *, unsigned, unsigned, unsigned);
int codec_plan_step(codec_plan_fixture_t *);
int codec_plan_check(codec_plan_fixture_t *);
#ifdef __cplusplus
}
#endif
#endif
