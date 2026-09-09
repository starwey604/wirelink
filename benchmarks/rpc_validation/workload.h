/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RPC_VALIDATION_WORKLOAD_H
#define RPC_VALIDATION_WORKLOAD_H
#include "validation.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  union { small_t small; large_t large; nested_t nested; } view;
  union { small_value_t small; large_value_t large; nested_value_t nested; } value;
  uint8_t input[NESTED_MAX_ENCODED_SIZE], canonical[NESTED_MAX_ENCODED_SIZE];
  size_t input_length, canonical_length, content_length;
  uint64_t fingerprint, expected_fingerprint;
  unsigned kind, stage;
} rpc_validation_fixture_t;
enum { RPC_VALIDATION_DECODE, RPC_VALIDATION_CANONICAL, RPC_VALIDATION_CONVERT,
       RPC_VALIDATION_PIPELINE, RPC_VALIDATION_OWNED_DECODE, RPC_VALIDATION_STAGES };
int rpc_validation_init(rpc_validation_fixture_t *, unsigned, unsigned, size_t);
int rpc_validation_step(rpc_validation_fixture_t *);
int rpc_validation_check(rpc_validation_fixture_t *);
#ifdef __cplusplus
}
#endif
#endif
