/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <string.h>

#define FINGERPRINT_SEED UINT64_C(0x24faaea3493c1c2e)

static uint64_t reference(const uint8_t *data, size_t length) {
  static const uint8_t domain[] = "wlc.rpc.canonical-request.v1";
  uint64_t hash = UINT64_C(0xcbf29ce484222325);
  for (size_t i = 0; i + 1U < sizeof(domain); ++i)
    hash = (hash ^ domain[i]) * UINT64_C(0x100000001b3);
  hash = (hash ^ 255U) * UINT64_C(0x100000001b3);
  for (size_t i = 0; i < length; ++i)
    hash = (hash ^ data[i]) * UINT64_C(0x100000001b3);
  return hash;
}

#if RPC_VALIDATION_ABI >= 29
/* Generator-private seam: never an application-facing API. */
#define DECLARE(name) \
  wl_codec_status_t name##_wlc_detail_fingerprint(const name##_t *, uint64_t *, size_t *); \
  void name##_wlc_detail_value_copy(const name##_t *, name##_value_t *);
DECLARE(small)
DECLARE(large)
DECLARE(nested)
#define CANONICAL(name) (f->fingerprint = FINGERPRINT_SEED, name##_wlc_detail_fingerprint(&f->view.name, &f->fingerprint, &f->canonical_length))
#define COPY(name) (name##_wlc_detail_value_copy(&f->view.name, &f->value.name), WL_CODEC_OK)
#define HASH_ENCODED() ((void)0)
#else
static uint64_t hash_encoded(const uint8_t *data, size_t length) {
  uint64_t hash = FINGERPRINT_SEED;
  for (size_t i = 0; i < length; ++i) hash = (hash ^ data[i]) * UINT64_C(0x100000001b3);
  return hash;
}
#define CANONICAL(name) name##_encode(&f->view.name, f->canonical, sizeof(f->canonical), &f->canonical_length)
#define COPY(name) name##_value_from_view(&f->view.name, &f->value.name)
#define HASH_ENCODED() (f->fingerprint = hash_encoded(f->canonical, f->canonical_length))
#endif

#define RUN(name) \
  if (f->stage == RPC_VALIDATION_OWNED_DECODE) \
    return name##_value_decode(f->input, f->input_length, &f->value.name); \
  if (f->stage == RPC_VALIDATION_DECODE || f->stage == RPC_VALIDATION_PIPELINE) { \
    error = name##_decode(f->input, f->input_length, &f->view.name); \
    if (error != WL_CODEC_OK || f->stage == RPC_VALIDATION_DECODE) return error; \
  } \
  if (f->stage == RPC_VALIDATION_CANONICAL || f->stage == RPC_VALIDATION_PIPELINE) { \
    error = CANONICAL(name); \
    if (error != WL_CODEC_OK) return error; \
    HASH_ENCODED(); \
  } \
  if (f->stage == RPC_VALIDATION_CONVERT) \
    return name##_value_from_view(&f->view.name, &f->value.name); \
  if (f->stage == RPC_VALIDATION_PIPELINE) { \
    error = COPY(name); \
    if (error != WL_CODEC_OK) return error; \
  }

int rpc_validation_step(rpc_validation_fixture_t *f) {
  int error = WL_CODEC_OK;
  switch (f->kind) {
    case 0: { RUN(small) break; }
    case 1: { RUN(large) break; }
    case 2: { RUN(nested) break; }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return error;
}

int rpc_validation_init(rpc_validation_fixture_t *f, unsigned kind, unsigned stage, size_t length) {
  if (kind > 2U || stage >= RPC_VALIDATION_STAGES || length > (kind == 0U ? 32U : 512U))
    return WL_CODEC_ERR_INVALID_VALUE;
  memset(f, 0, sizeof(*f));
  f->kind = kind; f->stage = stage; f->content_length = length;
  memset(f->canonical, 'a', sizeof(f->canonical));
  int error;
  if (kind == 0U) {
    small_clear(&f->view.small);
    f->view.small.has_text = f->view.small.has_body = true;
    f->view.small.text = (wl_codec_string_t){(const char *)f->canonical, length};
    f->view.small.body = (wl_codec_bytes_t){f->canonical, length};
    error = small_encode(&f->view.small, f->input, sizeof(f->input), &f->input_length);
  } else {
    large_t *v;
    if (kind == 1U) { large_clear(&f->view.large); v = &f->view.large; }
    else {
      nested_clear(&f->view.nested); v = &f->view.nested.child;
      f->view.nested.has_child = f->view.nested.has_samples = true;
      for (size_t i = 0; i < 128; ++i) f->view.nested.samples[i] = (float)i;
    }
    v->has_text = v->has_body = true;
    v->text = (wl_codec_string_t){(const char *)f->canonical, length};
    v->body = (wl_codec_bytes_t){f->canonical, length};
    error = kind == 1U ? large_encode(v, f->input, sizeof(f->input), &f->input_length)
        : nested_encode(&f->view.nested, f->input, sizeof(f->input), &f->input_length);
  }
  if (error != WL_CODEC_OK) return error;
  f->expected_fingerprint = reference(f->input, f->input_length);
  if (reference(NULL, 0) != FINGERPRINT_SEED) return WL_CODEC_ERR_INVALID_VALUE;
  /* Every steady-state stage starts from a decoded, independently owned input. */
  f->stage = RPC_VALIDATION_DECODE;
  error = rpc_validation_step(f);
  f->stage = stage;
  return error;
}

int rpc_validation_check(rpc_validation_fixture_t *f) {
  if ((f->stage == RPC_VALIDATION_CANONICAL || f->stage == RPC_VALIDATION_PIPELINE) &&
      (f->fingerprint != f->expected_fingerprint || f->canonical_length != f->input_length))
    return WL_CODEC_ERR_INVALID_VALUE;
  if (f->stage == RPC_VALIDATION_CONVERT || f->stage == RPC_VALIDATION_PIPELINE ||
      f->stage == RPC_VALIDATION_OWNED_DECODE) {
    const char *text = f->kind == 0 ? f->value.small.text.data :
        f->kind == 1 ? f->value.large.text.data : f->value.nested.child.text.data;
    for (size_t i = 0; i < f->content_length; ++i) if (text[i] != 'a') return WL_CODEC_ERR_INVALID_VALUE;
    if (text[f->content_length] != 0) return WL_CODEC_ERR_INVALID_VALUE;
    if (f->kind == 2 && f->value.nested.samples[127] != 127.0F) return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
