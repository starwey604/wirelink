/* RPC layout is a compilation setting, independent of schema/wire identity.
 * ELF markers preserve one input section per function. They do not select
 * a memory region or guarantee ordering. See wlc/docs/rpc-layout.md. */
#ifndef WLC_RPC_LAYOUT_ENABLE
#define WLC_RPC_LAYOUT_ENABLE 1
#define WLC_DEVICE_RPC_LAYOUT_GLOBAL_DEFAULT
#endif
#ifndef DEVICE_RPC_LAYOUT_ENABLE
#define DEVICE_RPC_LAYOUT_ENABLE WLC_RPC_LAYOUT_ENABLE
#define WLC_DEVICE_RPC_LAYOUT_MODULE_DEFAULT
#endif

/* Check in the preprocessor too: a C enum may be 1 in a static assertion
 * but is an unknown identifier (and therefore 0) in the layout #if. */
#define WLC_DEVICE_RPC_LAYOUT_VALUE_0 1
#define WLC_DEVICE_RPC_LAYOUT_VALUE_1 1
#define WLC_DEVICE_RPC_LAYOUT_VALUE_(value) WLC_DEVICE_RPC_LAYOUT_VALUE_##value
#define WLC_DEVICE_RPC_LAYOUT_VALUE(value) WLC_DEVICE_RPC_LAYOUT_VALUE_(value)
#if !(WLC_DEVICE_RPC_LAYOUT_VALUE(WLC_RPC_LAYOUT_ENABLE))
#error "WLC_RPC_LAYOUT_ENABLE must be literal 0 or 1"
#endif
#if !(WLC_DEVICE_RPC_LAYOUT_VALUE(DEVICE_RPC_LAYOUT_ENABLE))
#error "DEVICE_RPC_LAYOUT_ENABLE must be literal 0 or 1"
#endif
#undef WLC_DEVICE_RPC_LAYOUT_VALUE
#undef WLC_DEVICE_RPC_LAYOUT_VALUE_
#undef WLC_DEVICE_RPC_LAYOUT_VALUE_0
#undef WLC_DEVICE_RPC_LAYOUT_VALUE_1

#define WLC_DEVICE_RPC_LAYOUT_STRING_(value) #value
#define WLC_DEVICE_RPC_LAYOUT_STRING(value) WLC_DEVICE_RPC_LAYOUT_STRING_(value)
#if defined(__cplusplus)
#define WLC_DEVICE_RPC_LAYOUT_ASSERT static_assert
#else
#define WLC_DEVICE_RPC_LAYOUT_ASSERT _Static_assert
#endif
/* Check the whole expanded value: token-pasting alone only checks its first
 * token and can accidentally accept expressions such as 4/2. */
WLC_DEVICE_RPC_LAYOUT_ASSERT(
    sizeof(WLC_DEVICE_RPC_LAYOUT_STRING(WLC_RPC_LAYOUT_ENABLE)) == 2 &&
    (WLC_RPC_LAYOUT_ENABLE == 0 || WLC_RPC_LAYOUT_ENABLE == 1),
    "WLC_RPC_LAYOUT_ENABLE must be literal 0 or 1");
WLC_DEVICE_RPC_LAYOUT_ASSERT(
    sizeof(WLC_DEVICE_RPC_LAYOUT_STRING(DEVICE_RPC_LAYOUT_ENABLE)) == 2 &&
    (DEVICE_RPC_LAYOUT_ENABLE == 0 || DEVICE_RPC_LAYOUT_ENABLE == 1),
    "DEVICE_RPC_LAYOUT_ENABLE must be literal 0 or 1");
#undef WLC_DEVICE_RPC_LAYOUT_ASSERT
#undef WLC_DEVICE_RPC_LAYOUT_STRING
#undef WLC_DEVICE_RPC_LAYOUT_STRING_

#if DEVICE_RPC_LAYOUT_ENABLE && defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))
#define WLC_DEVICE_RPC_LAYOUT_ACTIVE 1
#ifndef DEVICE_RPC_LAYOUT
#define DEVICE_RPC_LAYOUT(function) __attribute__((section(".text.wlc.device.rpc." #function)))
#define WLC_DEVICE_RPC_LAYOUT_DEFAULT
#endif
#else
#define WLC_DEVICE_RPC_LAYOUT_ACTIVE 0
#endif

#include "device_runtime.h"

#include <string.h>

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_result)
DEVICE_RPC_LAYOUT_device_runtime_result
#endif
static device_runtime_result_t device_runtime_result(const wl_event_t *event) {
  device_runtime_result_t result = {0};
  result.domain = DEVICE_RUNTIME_INVALID_ARGUMENT;
  if (event != NULL) {
    result.message_id = event->message_id;
    result.event_type = event->type;
  }
  return result;
}

_Static_assert(DEVICE_SCHEMA_IDENTITY == UINT64_C(0x5A72C7FA9B968ADD) && DEVICE_BINDING_PROFILE_IDENTITY == UINT64_C(0x92F906C50720AF46), "Runtime source/header profile mismatch; regenerate the matched artifacts.");

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_result_str)
DEVICE_RPC_LAYOUT_device_runtime_result_str
#endif
const char *device_runtime_result_str(const device_runtime_result_t *result) {
  if (result == NULL) return "null result";
  switch (result->domain) {
    case DEVICE_RUNTIME_OK: return "ok";
    case DEVICE_RUNTIME_NON_RX: return "non-rx event";
    case DEVICE_RUNTIME_UNKNOWN_MESSAGE: return "unknown message";
    case DEVICE_RUNTIME_MISSING_ROUTE: return "missing route";
    case DEVICE_RUNTIME_MISSING_SCRATCH: return "missing scratch";
    case DEVICE_RUNTIME_DELIVERY_MISMATCH: return "delivery mismatch";
    case DEVICE_RUNTIME_CODEC_ERROR: return "codec error";
    case DEVICE_RUNTIME_STORAGE_ERROR: return "storage error";
    case DEVICE_RUNTIME_RPC_ERROR: return "rpc error";
    case DEVICE_RUNTIME_CORE_ERROR: return "core error";
    case DEVICE_RUNTIME_APPLICATION_ERROR: return "application error";
    case DEVICE_RUNTIME_INVALID_ARGUMENT: return "invalid argument";
    default: return "unknown runtime result";
  }
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_request_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_clear)
#endif
static inline void device_rpc_info_request_clear(info_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_request_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_encoded_size)
#endif
static inline size_t device_rpc_info_request_encoded_size(const info_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_encode)
#endif
static inline wl_codec_status_t device_rpc_info_request_encode(const info_request_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_decode)
#endif
static inline wl_codec_status_t device_rpc_info_request_decode(const uint8_t *, size_t, info_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_clear)
#endif
static inline void device_rpc_info_request_value_clear(info_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_encoded_size)
#endif
static inline size_t device_rpc_info_request_value_encoded_size(const info_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_encode)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_encode(const info_request_value_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_decode)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_decode(const uint8_t *, size_t, info_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_from_view(const info_request_t *, info_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_to_view(const info_request_value_t *, info_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_clear)
DEVICE_RPC_LAYOUT_device_rpc_settings_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_clear)
#endif
static inline void device_rpc_settings_clear(settings_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_settings_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_encoded_size)
#endif
static inline size_t device_rpc_settings_encoded_size(const settings_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_encode)
DEVICE_RPC_LAYOUT_device_rpc_settings_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_encode)
#endif
static inline wl_codec_status_t device_rpc_settings_encode(const settings_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_decode)
DEVICE_RPC_LAYOUT_device_rpc_settings_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_decode)
#endif
static inline wl_codec_status_t device_rpc_settings_decode(const uint8_t *, size_t, settings_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_clear)
#endif
static inline void device_rpc_settings_value_clear(settings_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_encoded_size)
#endif
static inline size_t device_rpc_settings_value_encoded_size(const settings_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_encode)
#endif
static inline wl_codec_status_t device_rpc_settings_value_encode(const settings_value_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_decode)
#endif
static inline wl_codec_status_t device_rpc_settings_value_decode(const uint8_t *, size_t, settings_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_settings_value_from_view(const settings_t *, settings_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_settings_value_to_view(const settings_value_t *, settings_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_response_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_clear)
#endif
static inline void device_rpc_info_response_clear(info_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_response_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_encoded_size)
#endif
static inline size_t device_rpc_info_response_encoded_size(const info_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_encode)
#endif
static inline wl_codec_status_t device_rpc_info_response_encode(const info_response_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_decode)
#endif
static inline wl_codec_status_t device_rpc_info_response_decode(const uint8_t *, size_t, info_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_clear)
#endif
static inline void device_rpc_info_response_value_clear(info_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_encoded_size)
#endif
static inline size_t device_rpc_info_response_value_encoded_size(const info_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_encode)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_encode(const info_response_value_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_decode)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_decode(const uint8_t *, size_t, info_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_from_view(const info_response_t *, info_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_to_view(const info_response_value_t *, info_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_clear)
#endif
static inline void device_rpc_configure_request_clear(configure_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_encoded_size)
#endif
static inline size_t device_rpc_configure_request_encoded_size(const configure_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_encode(const configure_request_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_decode(const uint8_t *, size_t, configure_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_clear)
#endif
static inline void device_rpc_configure_request_value_clear(configure_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_encoded_size)
#endif
static inline size_t device_rpc_configure_request_value_encoded_size(const configure_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_encode(const configure_request_value_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_decode(const uint8_t *, size_t, configure_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_from_view(const configure_request_t *, configure_request_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_to_view(const configure_request_value_t *, configure_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_clear)
#endif
static inline void device_rpc_configure_response_clear(configure_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_encoded_size)
#endif
static inline size_t device_rpc_configure_response_encoded_size(const configure_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_encode(const configure_response_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_decode(const uint8_t *, size_t, configure_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_clear)
#endif
static inline void device_rpc_configure_response_value_clear(configure_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_encoded_size)
#endif
static inline size_t device_rpc_configure_response_value_encoded_size(const configure_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_encode(const configure_response_value_t *, uint8_t *, size_t, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_decode(const uint8_t *, size_t, configure_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_from_view(const configure_response_t *, configure_response_value_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_to_view(const configure_response_value_t *, configure_response_t *);


#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
  DEVICE_RPC_WLC_OPTIONAL,
  DEVICE_RPC_WLC_REPEATED,
  DEVICE_RPC_WLC_PACKED,
  DEVICE_RPC_WLC_BOOL,
  DEVICE_RPC_WLC_U8,
  DEVICE_RPC_WLC_U16,
  DEVICE_RPC_WLC_U32,
  DEVICE_RPC_WLC_U64,
  DEVICE_RPC_WLC_I8,
  DEVICE_RPC_WLC_I16,
  DEVICE_RPC_WLC_I32,
  DEVICE_RPC_WLC_I64,
  DEVICE_RPC_WLC_F32,
  DEVICE_RPC_WLC_F64,
  DEVICE_RPC_WLC_FLOAT32,
  DEVICE_RPC_WLC_FLOAT64,
  DEVICE_RPC_WLC_BYTES,
  DEVICE_RPC_WLC_STRING,
  DEVICE_RPC_WLC_ENUM,
  DEVICE_RPC_WLC_MESSAGE
};
typedef struct device_rpc_wlc_desc device_rpc_wlc_desc_t;
typedef struct {
  uint16_t number;
  uint8_t card, kind, required, wire, key_size;
  size_t value, has, count, capacity, element, packed_count;
  uint16_t max_length;
  uint8_t key[3];
  int64_t signed_default;
  uint64_t unsigned_default;
  const char *string_default;
  const device_rpc_wlc_desc_t *nested;
} device_rpc_wlc_field_t;
enum { DEVICE_RPC_WLC_LOOKUP_LINEAR, DEVICE_RPC_WLC_LOOKUP_DENSE, DEVICE_RPC_WLC_LOOKUP_BINARY };
struct device_rpc_wlc_desc { const device_rpc_wlc_field_t *fields; size_t count; uint8_t lookup; };

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_add)
DEVICE_RPC_LAYOUT_device_rpc_wlc_add
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_add)
#endif
static inline wl_codec_status_t device_rpc_wlc_add(size_t *a, size_t b) {
  if (b > SIZE_MAX - *a) return WL_CODEC_ERR_OVERFLOW;
  *a += b;
  return WL_CODEC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_vsize)
DEVICE_RPC_LAYOUT_device_rpc_wlc_vsize
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_vsize)
#endif
static inline size_t device_rpc_wlc_vsize(uint64_t v) {
  size_t n = 1U;
  while (v >= 128U) { v >>= 7U; ++n; }
  return n;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_putv)
DEVICE_RPC_LAYOUT_device_rpc_wlc_putv
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_putv)
#endif
static inline void device_rpc_wlc_putv(uint8_t **p, uint64_t v) {
  while (v >= 128U) { *(*p)++ = (uint8_t)(v | 128U); v >>= 7U; }
  *(*p)++ = (uint8_t)v;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_getv)
DEVICE_RPC_LAYOUT_device_rpc_wlc_getv
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_getv)
#endif
static wl_codec_status_t device_rpc_wlc_getv(const uint8_t *in, size_t length, size_t *at,
                                  uint64_t *out) {
  size_t start = *at, n = 0U;
  uint64_t v = 0U;
  while (*at < length && n < 10U) {
    uint8_t b = in[(*at)++];
    if (n == 9U && b > 1U) return WL_CODEC_ERR_OVERFLOW;
    v |= (uint64_t)(b & 127U) << (7U * n++);
    if ((b & 128U) == 0U) {
      if (device_rpc_wlc_vsize(v) != *at - start) return WL_CODEC_ERR_MALFORMED;
      *out = v;
      return WL_CODEC_OK;
    }
  }
  return *at == length ? WL_CODEC_ERR_MALFORMED : WL_CODEC_ERR_OVERFLOW;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_vsize32)
DEVICE_RPC_LAYOUT_device_rpc_wlc_vsize32
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_vsize32)
#endif
static inline size_t device_rpc_wlc_vsize32(uint32_t value) {
  size_t size = 1U;
  while (value >= 128U) { value >>= 7U; ++size; }
  return size;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_putv32)
DEVICE_RPC_LAYOUT_device_rpc_wlc_putv32
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_putv32)
#endif
static inline void device_rpc_wlc_putv32(uint8_t **cursor, uint32_t value) {
  while (value >= 128U) {
    *(*cursor)++ = (uint8_t)(value | 128U);
    value >>= 7U;
  }
  *(*cursor)++ = (uint8_t)value;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_utf8)
DEVICE_RPC_LAYOUT_device_rpc_wlc_utf8
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_utf8)
#endif
static bool device_rpc_wlc_utf8(const uint8_t *s, size_t n) {
  size_t i = 0U;
  while (i < n) {
    uint8_t a = s[i++];
    if (a < 0x80U) continue;
    size_t need;
    uint32_t v;
    if (a >= 0xC2U && a <= 0xDFU) { need = 1U; v = a & 0x1FU; }
    else if (a >= 0xE0U && a <= 0xEFU) { need = 2U; v = a & 0x0FU; }
    else if (a >= 0xF0U && a <= 0xF4U) { need = 3U; v = a & 0x07U; }
    else return false;
    if (need > n - i) return false;
    while (need-- != 0U) {
      uint8_t b = s[i++];
      if ((b & 0xC0U) != 0x80U) return false;
      v = (v << 6U) | (b & 0x3FU);
    }
    if ((a == 0xE0U && v < 0x800U) ||
        (a == 0xEDU && v >= 0xD800U) ||
        (a == 0xF0U && v < 0x10000U) ||
        (a == 0xF4U && v > 0x10FFFFU)) return false;
  }
  return true;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_wire)
DEVICE_RPC_LAYOUT_device_rpc_wlc_wire
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_wire)
#endif
static inline uint8_t device_rpc_wlc_wire(const device_rpc_wlc_field_t *f) {
  return f->wire;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_z32)
DEVICE_RPC_LAYOUT_device_rpc_wlc_z32
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_z32)
#endif
static inline uint64_t device_rpc_wlc_z32(int32_t v) {
  return ((uint32_t)v << 1U) ^ (uint32_t)-(uint32_t)(v < 0);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_z64)
DEVICE_RPC_LAYOUT_device_rpc_wlc_z64
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_z64)
#endif
static inline uint64_t device_rpc_wlc_z64(int64_t v) {
  return ((uint64_t)v << 1U) ^ (uint64_t)-(uint64_t)(v < 0);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_uz32)
DEVICE_RPC_LAYOUT_device_rpc_wlc_uz32
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_uz32)
#endif
static inline int32_t device_rpc_wlc_uz32(uint32_t v) {
  return (int32_t)((v >> 1U) ^ (uint32_t)-(v & 1U));
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_uz64)
DEVICE_RPC_LAYOUT_device_rpc_wlc_uz64
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_uz64)
#endif
static inline int64_t device_rpc_wlc_uz64(uint64_t v) {
  return (int64_t)((v >> 1U) ^ (uint64_t)-(v & 1U));
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_measure)
DEVICE_RPC_LAYOUT_device_rpc_wlc_measure
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_measure)
#endif
static inline wl_codec_status_t device_rpc_wlc_measure(const device_rpc_wlc_desc_t *, const void *, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_measure_impl)
DEVICE_RPC_LAYOUT_device_rpc_wlc_measure_impl
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_measure_impl)
#endif
static wl_codec_status_t device_rpc_wlc_measure_impl(const device_rpc_wlc_desc_t *, const void *, size_t *, bool);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_clear)
DEVICE_RPC_LAYOUT_device_rpc_wlc_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_clear)
#endif
static void device_rpc_wlc_clear(const device_rpc_wlc_desc_t *, void *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_decode)
DEVICE_RPC_LAYOUT_device_rpc_wlc_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_decode)
#endif
static inline wl_codec_status_t device_rpc_wlc_decode(const device_rpc_wlc_desc_t *, const uint8_t *, size_t,
                                    void *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_fields)
DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_fields
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_emit_fields)
#endif
static wl_codec_status_t device_rpc_wlc_emit_fields(const device_rpc_wlc_desc_t *, const void *,
                                         uint8_t **);

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_packed_bytes)
DEVICE_RPC_LAYOUT_device_rpc_wlc_packed_bytes
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_packed_bytes)
#endif
static wl_codec_status_t device_rpc_wlc_packed_bytes(const device_rpc_wlc_field_t *f, size_t *bytes) {
  if (f->packed_count != 0U && f->element > SIZE_MAX / f->packed_count)
    return WL_CODEC_ERR_OVERFLOW;
  *bytes = f->element * f->packed_count;
  return WL_CODEC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_body)
DEVICE_RPC_LAYOUT_device_rpc_wlc_body
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_body)
#endif
static wl_codec_status_t device_rpc_wlc_body(const device_rpc_wlc_field_t *f, const void *p,
                                  size_t *n, bool validated) {
  (void)p;
  (void)validated;
  *n = 0U;
  switch (f->kind) {
    case DEVICE_RPC_WLC_BOOL: {
      bool v = *(const bool *)p;
      if (v != false && v != true) return WL_CODEC_ERR_INVALID_VALUE;
      *n = 1U;
      return WL_CODEC_OK;
    }
    case DEVICE_RPC_WLC_U16: *n = device_rpc_wlc_vsize(*(const uint16_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U32: *n = device_rpc_wlc_vsize(*(const uint32_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U64: *n = device_rpc_wlc_vsize(*(const uint64_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_ENUM:
      *n = device_rpc_wlc_vsize(device_rpc_wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_I64: *n = device_rpc_wlc_vsize(device_rpc_wlc_z64(*(const int64_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_FLOAT32:
      *n = 4U; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_F64:
      *n = 8U; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      if (f->max_length != 0U && v->length > (size_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v->length != 0U && v->data == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      if (device_rpc_wlc_add(n, device_rpc_wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return device_rpc_wlc_add(n, v->length);
    }
    case DEVICE_RPC_WLC_STRING: {
      const wl_codec_string_t *v = p;
      if (f->max_length != 0U && v->length > (size_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v->length != 0U && v->data == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      if (!validated && !device_rpc_wlc_utf8((const uint8_t *)v->data, v->length)) return WL_CODEC_ERR_UTF8;
      if (device_rpc_wlc_add(n, device_rpc_wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return device_rpc_wlc_add(n, v->length);
    }
    case DEVICE_RPC_WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = device_rpc_wlc_measure_impl(f->nested, p, &child, validated);
      if (s != WL_CODEC_OK) return s;
      *n = device_rpc_wlc_vsize(child);
      return device_rpc_wlc_add(n, child);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_measure_impl)
DEVICE_RPC_LAYOUT_device_rpc_wlc_measure_impl
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_measure_impl)
#endif
static wl_codec_status_t device_rpc_wlc_measure_impl(const device_rpc_wlc_desc_t *d, const void *value,
                                     size_t *out, bool validated) {
  size_t n = 0U;
  if (d == NULL || value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      size_t bytes;
      wl_codec_status_t s;
      if (!*(const bool *)(base + f->has)) {
        if (f->required != 0U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
        continue;
      }
      if ((s = device_rpc_wlc_packed_bytes(f, &bytes)) != WL_CODEC_OK) return s;
      if ((s = device_rpc_wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = device_rpc_wlc_add(&n, device_rpc_wlc_vsize(bytes))) != WL_CODEC_OK ||
          (s = device_rpc_wlc_add(&n, bytes)) != WL_CODEC_OK) return s;
      continue;
    }
    size_t count = 1U;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) {
      if (!*(const bool *)(base + f->has)) {
        if (f->required != 0U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
        continue;
      }
    } else {
      count = *(const size_t *)(base + f->count);
      if ((count != 0U && *(void *const *)(base + f->value) == NULL) ||
          count > *(const size_t *)(base + f->capacity))
        return WL_CODEC_ERR_INVALID_VALUE;
    }
    for (size_t j = 0U; j < count; ++j) {
      size_t body;
      const void *p = f->card == DEVICE_RPC_WLC_REPEATED
                          ? *(const uint8_t *const *)(base + f->value) + j * f->element
                          : base + f->value;
      wl_codec_status_t s = device_rpc_wlc_body(f, p, &body, validated);
      if (s != WL_CODEC_OK) return s;
      if ((s = device_rpc_wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = device_rpc_wlc_add(&n, body)) != WL_CODEC_OK) return s;
    }
  }
  *out = n;
  return WL_CODEC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_measure)
DEVICE_RPC_LAYOUT_device_rpc_wlc_measure
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_measure)
#endif
static inline wl_codec_status_t device_rpc_wlc_measure(const device_rpc_wlc_desc_t *d, const void *value, size_t *out) {
  return device_rpc_wlc_measure_impl(d, value, out, false);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_clear)
DEVICE_RPC_LAYOUT_device_rpc_wlc_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_clear)
#endif
static void device_rpc_wlc_clear(const device_rpc_wlc_desc_t *d, void *value) {
  uint8_t *base = value;
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    void *p = base + f->value;
    (void)p;
    if (f->card == DEVICE_RPC_WLC_REPEATED) {
      *(size_t *)(base + f->count) = 0U;
      continue;
    }
    *(bool *)(base + f->has) = false;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      memset(p, 0, f->element * f->packed_count);
      continue;
    }
    switch (f->kind) {
    case DEVICE_RPC_WLC_MESSAGE: device_rpc_wlc_clear(f->nested, p); break;
    case DEVICE_RPC_WLC_STRING:
      *(wl_codec_string_t *)p = (wl_codec_string_t){f->string_default, (size_t)f->unsigned_default}; break;
    case DEVICE_RPC_WLC_BYTES: *(wl_codec_bytes_t *)p = (wl_codec_bytes_t){NULL, 0U}; break;
    case DEVICE_RPC_WLC_FLOAT32:
      memset(p, 0, f->element); break;
    case DEVICE_RPC_WLC_BOOL: *(bool *)p = f->unsigned_default != 0U; break;
    case DEVICE_RPC_WLC_ENUM:
      *(int32_t *)p = (int32_t)f->signed_default; break;
    case DEVICE_RPC_WLC_I64: *(int64_t *)p = f->signed_default; break;
    case DEVICE_RPC_WLC_U16: *(uint16_t *)p = (uint16_t)f->unsigned_default; break;
    case DEVICE_RPC_WLC_U32:
      *(uint32_t *)p = (uint32_t)f->unsigned_default; break;
    case DEVICE_RPC_WLC_U64:
    case DEVICE_RPC_WLC_F64:
      *(uint64_t *)p = f->unsigned_default; break;
    default: break;
    }
  }
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_put32)
DEVICE_RPC_LAYOUT_device_rpc_wlc_put32
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_put32)
#endif
static inline void device_rpc_wlc_put32(uint8_t **p, uint32_t v) {
  *(*p)++ = (uint8_t)(v >> 24U);
  *(*p)++ = (uint8_t)(v >> 16U);
  *(*p)++ = (uint8_t)(v >> 8U);
  *(*p)++ = (uint8_t)v;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_put64)
DEVICE_RPC_LAYOUT_device_rpc_wlc_put64
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_put64)
#endif
static inline void device_rpc_wlc_put64(uint8_t **p, uint64_t v) {
  device_rpc_wlc_put32(p, (uint32_t)(v >> 32U));
  device_rpc_wlc_put32(p, (uint32_t)v);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_copy_span)
DEVICE_RPC_LAYOUT_device_rpc_wlc_copy_span
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_copy_span)
#endif
static inline void device_rpc_wlc_copy_span(uint8_t **out, const void *data, size_t length) {
  if (length != 0U) { memcpy(*out, data, length); *out += length; }
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_put_key)
DEVICE_RPC_LAYOUT_device_rpc_wlc_put_key
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_put_key)
#endif
static inline void device_rpc_wlc_put_key(uint8_t **out, const device_rpc_wlc_field_t *f) {
  *(*out)++ = f->key[0];
  if (f->key_size > 1U) {
    *(*out)++ = f->key[1];
    if (f->key_size > 2U) *(*out)++ = f->key[2];
  }
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_fixed)
DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_fixed
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_emit_fixed)
#endif
static wl_codec_status_t device_rpc_wlc_emit_fixed(uint8_t kind, const void *value,
                                        uint8_t **out) {
  switch (kind) {
    case DEVICE_RPC_WLC_F64: device_rpc_wlc_put64(out, *(const uint64_t *)value); break;
    case DEVICE_RPC_WLC_FLOAT32: {
      uint32_t bits32;
      memcpy(&bits32, value, sizeof(bits32));
      device_rpc_wlc_put32(out, bits32); break;
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_value)
DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_value
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_emit_value)
#endif
static wl_codec_status_t device_rpc_wlc_emit_value(const device_rpc_wlc_field_t *f, const void *p,
                                        uint8_t **out) {
  (void)p; (void)out;
  switch (f->kind) {
    case DEVICE_RPC_WLC_BOOL: device_rpc_wlc_putv(out, *(const bool *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U16: device_rpc_wlc_putv(out, *(const uint16_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U32: device_rpc_wlc_putv(out, *(const uint32_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U64: device_rpc_wlc_putv(out, *(const uint64_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_ENUM:
      device_rpc_wlc_putv(out, device_rpc_wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_I64: device_rpc_wlc_putv(out, device_rpc_wlc_z64(*(const int64_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_F64:
    case DEVICE_RPC_WLC_FLOAT32:
      return device_rpc_wlc_emit_fixed(f->kind, p, out);
    case DEVICE_RPC_WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      device_rpc_wlc_putv(out, v->length);
      device_rpc_wlc_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case DEVICE_RPC_WLC_STRING: {
      const wl_codec_string_t *v = p;
      device_rpc_wlc_putv(out, v->length);
      device_rpc_wlc_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case DEVICE_RPC_WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = device_rpc_wlc_measure(f->nested, p, &child);
      if (s != WL_CODEC_OK) return s;
      device_rpc_wlc_putv(out, child);
      return device_rpc_wlc_emit_fields(f->nested, p, out);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_packed)
DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_packed
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_emit_packed)
#endif
static wl_codec_status_t device_rpc_wlc_emit_packed(const device_rpc_wlc_field_t *f, const void *p,
                                         uint8_t **out) {
  size_t bytes;
  wl_codec_status_t s = device_rpc_wlc_packed_bytes(f, &bytes);
  if (s != WL_CODEC_OK) return s;
  device_rpc_wlc_putv(out, bytes);
  if (f->kind == DEVICE_RPC_WLC_F32 || f->kind == DEVICE_RPC_WLC_FLOAT32) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint32_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      device_rpc_wlc_put32(out, bits);
    }
    return WL_CODEC_OK;
  }
  if (f->kind == DEVICE_RPC_WLC_F64 || f->kind == DEVICE_RPC_WLC_FLOAT64) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint64_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      device_rpc_wlc_put64(out, bits);
    }
    return WL_CODEC_OK;
  }
  return WL_CODEC_ERR_INVALID_VALUE;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_fields)
DEVICE_RPC_LAYOUT_device_rpc_wlc_emit_fields
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_emit_fields)
#endif
static inline wl_codec_status_t device_rpc_wlc_emit_fields(const device_rpc_wlc_desc_t *d, const void *value,
                                         uint8_t **out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      wl_codec_status_t s;
      if (!*(const bool *)(base + f->has)) continue;
      device_rpc_wlc_put_key(out, f);
      if ((s = device_rpc_wlc_emit_packed(f, base + f->value, out)) != WL_CODEC_OK) return s;
      continue;
    }
    size_t count = f->card == DEVICE_RPC_WLC_OPTIONAL
                       ? (*(const bool *)(base + f->has) ? 1U : 0U)
                       : *(const size_t *)(base + f->count);
    for (size_t j = 0U; j < count; ++j) {
      const void *p = f->card == DEVICE_RPC_WLC_REPEATED
                          ? *(const uint8_t *const *)(base + f->value) + j * f->element
                          : base + f->value;
      wl_codec_status_t s;
      device_rpc_wlc_put_key(out, f);
      if ((s = device_rpc_wlc_emit_value(f, p, out)) != WL_CODEC_OK) return s;
    }
  }
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_encode)
DEVICE_RPC_LAYOUT_device_rpc_wlc_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_encode)
#endif
static inline wl_codec_status_t device_rpc_wlc_encode(const device_rpc_wlc_desc_t *d, const void *value,
                                    uint8_t *out, size_t cap, size_t *length) {
  size_t n;
  wl_codec_status_t s = device_rpc_wlc_measure(d, value, &n);
  if (s != WL_CODEC_OK || length == NULL || (n != 0U && out == NULL))
    return s == WL_CODEC_OK ? WL_CODEC_ERR_INVALID_VALUE : s;
  if (cap < n) return WL_CODEC_ERR_CAPACITY;
  uint8_t *p = out;
  if ((s = device_rpc_wlc_emit_fields(d, value, &p)) != WL_CODEC_OK) return s;
  *length = n;
  return WL_CODEC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_skip)
DEVICE_RPC_LAYOUT_device_rpc_wlc_skip
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_skip)
#endif
static wl_codec_status_t device_rpc_wlc_skip(uint8_t wire, const uint8_t *in, size_t n,
                                  size_t *at) {
  uint64_t length;
  wl_codec_status_t s;
  if (wire == 0U) return device_rpc_wlc_getv(in, n, at, &length);
  if (wire == 1U) {
    if (n - *at < 8U) return WL_CODEC_ERR_MALFORMED;
    *at += 8U;
    return WL_CODEC_OK;
  }
  if (wire == 5U) {
    if (n - *at < 4U) return WL_CODEC_ERR_MALFORMED;
    *at += 4U;
    return WL_CODEC_OK;
  }
  if (wire != 2U) return WL_CODEC_ERR_MALFORMED;
  if ((s = device_rpc_wlc_getv(in, n, at, &length)) != WL_CODEC_OK) return s;
  if (length > n - *at) return WL_CODEC_ERR_MALFORMED;
  *at += (size_t)length;
  return WL_CODEC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_read_fixed)
DEVICE_RPC_LAYOUT_device_rpc_wlc_read_fixed
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_read_fixed)
#endif
static wl_codec_status_t device_rpc_wlc_read_fixed(uint8_t kind, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  size_t bytes = (kind == DEVICE_RPC_WLC_F32 || kind == DEVICE_RPC_WLC_FLOAT32) ? 4U : 8U;
  if (n - *at < bytes) return WL_CODEC_ERR_MALFORMED;
  uint64_t bits = 0U;
  for (size_t i = 0U; i < bytes; ++i) bits = (bits << 8U) | in[(*at)++];
  switch (kind) {
    case DEVICE_RPC_WLC_F64: *(uint64_t *)out = bits; break;
    case DEVICE_RPC_WLC_FLOAT32: {
      uint32_t bits32 = (uint32_t)bits;
      memcpy(out, &bits32, sizeof(bits32)); break;
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_read_value)
DEVICE_RPC_LAYOUT_device_rpc_wlc_read_value
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_read_value)
#endif
static wl_codec_status_t device_rpc_wlc_read_value(const device_rpc_wlc_field_t *f, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  (void)in; (void)n; (void)at; (void)out;
  switch (f->kind) {
    case DEVICE_RPC_WLC_F64:
    case DEVICE_RPC_WLC_FLOAT32:
      return device_rpc_wlc_read_fixed(f->kind, in, n, at, out);
    case DEVICE_RPC_WLC_BYTES:
    case DEVICE_RPC_WLC_STRING:
    case DEVICE_RPC_WLC_MESSAGE:
      {
      uint64_t v;
      wl_codec_status_t s = device_rpc_wlc_getv(in, n, at, &v);
      if (s != WL_CODEC_OK) return s;
      if ((f->kind == DEVICE_RPC_WLC_BYTES || f->kind == DEVICE_RPC_WLC_STRING) &&
          f->max_length != 0U && v > (uint64_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v > n - *at) return WL_CODEC_ERR_MALFORMED;
      size_t bytes = (size_t)v;
      if (f->kind == DEVICE_RPC_WLC_BYTES)
        *(wl_codec_bytes_t *)out = (wl_codec_bytes_t){in + *at, bytes};
      if (f->kind == DEVICE_RPC_WLC_STRING) {
        if (!device_rpc_wlc_utf8(in + *at, bytes)) return WL_CODEC_ERR_UTF8;
        *(wl_codec_string_t *)out = (wl_codec_string_t){(const char *)(in + *at), bytes};
      }
      if (f->kind == DEVICE_RPC_WLC_MESSAGE) {
        s = device_rpc_wlc_decode(f->nested, in + *at, bytes, out);
        if (s != WL_CODEC_OK) return s;
      }
      *at += bytes;
      return WL_CODEC_OK;
    }
    default: break;
  }
  uint64_t v;
  wl_codec_status_t s = device_rpc_wlc_getv(in, n, at, &v);
  if (s != WL_CODEC_OK) return s;
  switch (f->kind) {
    case DEVICE_RPC_WLC_BOOL:
      if (v > 1U) return WL_CODEC_ERR_INVALID_VALUE;
      *(bool *)out = v != 0U; break;
    case DEVICE_RPC_WLC_U16:
      if (v > UINT16_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(uint16_t *)out = (uint16_t)v; break;
    case DEVICE_RPC_WLC_U32:
      if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(uint32_t *)out = (uint32_t)v; break;
    case DEVICE_RPC_WLC_U64: *(uint64_t *)out = v; break;
    case DEVICE_RPC_WLC_ENUM:

      if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(int32_t *)out = device_rpc_wlc_uz32((uint32_t)v); break;
    case DEVICE_RPC_WLC_I64: *(int64_t *)out = device_rpc_wlc_uz64(v); break;
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
  return WL_CODEC_ERR_INVALID_VALUE;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_read_packed)
DEVICE_RPC_LAYOUT_device_rpc_wlc_read_packed
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_read_packed)
#endif
static wl_codec_status_t device_rpc_wlc_read_packed(const device_rpc_wlc_field_t *f,
                                         const uint8_t *in, size_t n,
                                         size_t *at, void *out) {
  uint64_t encoded_bytes;
  size_t expected_bytes;
  wl_codec_status_t s;
  if ((s = device_rpc_wlc_getv(in, n, at, &encoded_bytes)) != WL_CODEC_OK) return s;
  if ((s = device_rpc_wlc_packed_bytes(f, &expected_bytes)) != WL_CODEC_OK) return s;
  if (encoded_bytes != expected_bytes || expected_bytes > n - *at)
    return WL_CODEC_ERR_MALFORMED;
  if (f->kind == DEVICE_RPC_WLC_F32 || f->kind == DEVICE_RPC_WLC_FLOAT32) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint32_t bits = ((uint32_t)in[*at] << 24U) |
                      ((uint32_t)in[*at + 1U] << 16U) |
                      ((uint32_t)in[*at + 2U] << 8U) |
                      (uint32_t)in[*at + 3U];
      memcpy((uint8_t *)out + j * f->element, &bits, sizeof(bits));
      *at += 4U;
    }
    return WL_CODEC_OK;
  }
  if (f->kind == DEVICE_RPC_WLC_F64 || f->kind == DEVICE_RPC_WLC_FLOAT64) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint64_t bits = 0U;
      for (size_t i = 0U; i < 8U; ++i) bits = (bits << 8U) | in[(*at)++];
      memcpy((uint8_t *)out + j * f->element, &bits, sizeof(bits));
    }
    return WL_CODEC_OK;
  }
  return WL_CODEC_ERR_INVALID_VALUE;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_find_field)
DEVICE_RPC_LAYOUT_device_rpc_wlc_find_field
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_find_field)
#endif
static const device_rpc_wlc_field_t *device_rpc_wlc_find_field(const device_rpc_wlc_desc_t *d, uint16_t number) {
  if (d->lookup == DEVICE_RPC_WLC_LOOKUP_DENSE) {
    size_t index = (size_t)number - (size_t)d->fields[0].number;
    return index < d->count ? &d->fields[index] : NULL;
  }
  if (d->lookup == DEVICE_RPC_WLC_LOOKUP_BINARY) {
    size_t lo = 0U, hi = d->count;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2U;
      uint16_t candidate = d->fields[mid].number;
      if (candidate == number) return &d->fields[mid];
      if (candidate < number) lo = mid + 1U;
      else hi = mid;
    }
    return NULL;
  }
  for (size_t i = 0U; i < d->count; ++i)
    if (d->fields[i].number == number) return &d->fields[i];
  return NULL;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_decode)
DEVICE_RPC_LAYOUT_device_rpc_wlc_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_decode)
#endif
static inline wl_codec_status_t device_rpc_wlc_decode(const device_rpc_wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  device_rpc_wlc_clear(d, out);
  // A hint only: missing, unknown or reordered fields still use exact lookup.
  // Each recursive decode owns its cursor; empty schemas avoid NULL arithmetic.
  const device_rpc_wlc_field_t *next = d->fields;
  const device_rpc_wlc_field_t *end = d->count == 0U ? next : next + d->count;
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = device_rpc_wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t raw_number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    uint16_t number = (uint16_t)raw_number;
    const device_rpc_wlc_field_t *f = next != end && next->number == number
                               ? next : device_rpc_wlc_find_field(d, number);
    if (f == NULL) {
      if ((s = device_rpc_wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != device_rpc_wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
    next = f->card == DEVICE_RPC_WLC_REPEATED ? f : f + 1;
    uint8_t *base = out;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
      if ((s = device_rpc_wlc_read_packed(f, in, n, &at, base + f->value)) != WL_CODEC_OK)
        return s;
      *(bool *)(base + f->has) = true;
      continue;
    }
    void *p = base + f->value;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = device_rpc_wlc_read_value(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static const device_rpc_wlc_desc_t device_rpc_info_request_desc;
static const device_rpc_wlc_desc_t device_rpc_settings_desc;
static const device_rpc_wlc_desc_t device_rpc_info_response_desc;
static const device_rpc_wlc_desc_t device_rpc_configure_request_desc;
static const device_rpc_wlc_desc_t device_rpc_configure_response_desc;

static const device_rpc_wlc_desc_t device_rpc_info_request_desc = { NULL, 0U, DEVICE_RPC_WLC_LOOKUP_LINEAR };

static const device_rpc_wlc_field_t device_rpc_settings_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_t, label), offsetof(settings_t, has_label), 0, 0, sizeof(wl_codec_string_t), 0U, 12U, { 10U, 0U, 0U }, 0, 6ULL, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BOOL, 1, 0U, 1U, offsetof(settings_t, enabled), offsetof(settings_t, has_enabled), 0, 0, sizeof(bool), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_ENUM, 0, 0U, 1U, offsetof(settings_t, mode), offsetof(settings_t, has_mode), 0, 0, sizeof(mode_t), 0U, 0U, { 24U, 0U, 0U }, INT32_C(1), 0ULL, NULL, NULL },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 1, 2U, 1U, offsetof(settings_t, token), offsetof(settings_t, has_token), 0, 0, sizeof(wl_codec_bytes_t), 0U, 8U, { 34U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_FLOAT32, 1, 2U, 1U, offsetof(settings_t, gains), offsetof(settings_t, has_gains), 0, 0, sizeof(float), 3U, 0U, { 42U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 6U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_F64, 0, 2U, 1U, offsetof(settings_t, counters), offsetof(settings_t, has_counters), 0, 0, sizeof(uint64_t), 2U, 0U, { 50U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 7U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_I64, 0, 0U, 1U, offsetof(settings_t, floor), offsetof(settings_t, has_floor), 0, 0, sizeof(int64_t), 0U, 0U, { 56U, 0U, 0U }, INT64_MIN, 0ULL, NULL, NULL },
  { 8U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U64, 0, 0U, 1U, offsetof(settings_t, ceiling), offsetof(settings_t, has_ceiling), 0, 0, sizeof(uint64_t), 0U, 0U, { 64U, 0U, 0U }, 0, 18446744073709551615ULL, NULL, NULL },
  { 9U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_t, class_), offsetof(settings_t, has_class_), 0, 0, sizeof(wl_codec_string_t), 0U, 8U, { 74U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 10U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U16, 0, 0U, 1U, offsetof(settings_t, timeout), offsetof(settings_t, has_timeout), 0, 0, sizeof(uint16_t), 0U, 0U, { 80U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_desc_t device_rpc_settings_desc = { device_rpc_settings_fields, sizeof(device_rpc_settings_fields) / sizeof(device_rpc_settings_fields[0]), DEVICE_RPC_WLC_LOOKUP_DENSE };

static const device_rpc_wlc_field_t device_rpc_info_response_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 1, 2U, 1U, offsetof(info_response_t, name), offsetof(info_response_t, has_name), 0, 0, sizeof(wl_codec_string_t), 0U, 32U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(info_response_t, settings), offsetof(info_response_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 18U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
};
static const device_rpc_wlc_desc_t device_rpc_info_response_desc = { device_rpc_info_response_fields, sizeof(device_rpc_info_response_fields) / sizeof(device_rpc_info_response_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR };

static const device_rpc_wlc_field_t device_rpc_configure_request_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_request_t, settings), offsetof(configure_request_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_request_t, opaque), offsetof(configure_request_t, has_opaque), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_request_t, backup), offsetof(configure_request_t, has_backup), 0, 0, sizeof(settings_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
};
static const device_rpc_wlc_desc_t device_rpc_configure_request_desc = { device_rpc_configure_request_fields, sizeof(device_rpc_configure_request_fields) / sizeof(device_rpc_configure_request_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR };

static const device_rpc_wlc_field_t device_rpc_configure_response_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_response_t, settings), offsetof(configure_response_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_response_t, opaque), offsetof(configure_response_t, has_opaque), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_response_t, backup), offsetof(configure_response_t, has_backup), 0, 0, sizeof(settings_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U32, 1, 0U, 1U, offsetof(configure_response_t, count), offsetof(configure_response_t, has_count), 0, 0, sizeof(uint32_t), 0U, 0U, { 32U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_desc_t device_rpc_configure_response_desc = { device_rpc_configure_response_fields, sizeof(device_rpc_configure_response_fields) / sizeof(device_rpc_configure_response_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR };

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_measure)
DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_measure
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_wlc_detail_measure)
#endif
static inline wl_codec_status_t device_rpc_info_request_wlc_detail_measure(const info_request_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_info_request_desc, value, size); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_request_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_clear)
#endif
static inline void device_rpc_info_request_clear(info_request_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_info_request_desc, value); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_request_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_encoded_size)
#endif
static inline size_t device_rpc_info_request_encoded_size(const info_request_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_info_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_encode)
#endif
static inline wl_codec_status_t device_rpc_info_request_encode(const info_request_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_info_request_desc, value, out, cap, length); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_decode)
#endif
static inline wl_codec_status_t device_rpc_info_request_decode(const uint8_t *input, size_t length, info_request_t *out) { return device_rpc_wlc_decode(&device_rpc_info_request_desc, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_wlc_detail_measure)
DEVICE_RPC_LAYOUT_device_rpc_settings_wlc_detail_measure
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_wlc_detail_measure)
#endif
static inline wl_codec_status_t device_rpc_settings_wlc_detail_measure(const settings_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_settings_desc, value, size); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_clear)
DEVICE_RPC_LAYOUT_device_rpc_settings_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_clear)
#endif
static inline void device_rpc_settings_clear(settings_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_settings_desc, value); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_settings_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_encoded_size)
#endif
static inline size_t device_rpc_settings_encoded_size(const settings_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_settings_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_encode)
DEVICE_RPC_LAYOUT_device_rpc_settings_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_encode)
#endif
static inline wl_codec_status_t device_rpc_settings_encode(const settings_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_settings_desc, value, out, cap, length); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_decode)
DEVICE_RPC_LAYOUT_device_rpc_settings_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_decode)
#endif
static inline wl_codec_status_t device_rpc_settings_decode(const uint8_t *input, size_t length, settings_t *out) { return device_rpc_wlc_decode(&device_rpc_settings_desc, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_wlc_detail_measure)
DEVICE_RPC_LAYOUT_device_rpc_info_response_wlc_detail_measure
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_wlc_detail_measure)
#endif
static inline wl_codec_status_t device_rpc_info_response_wlc_detail_measure(const info_response_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_info_response_desc, value, size); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_response_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_clear)
#endif
static inline void device_rpc_info_response_clear(info_response_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_info_response_desc, value); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_response_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_encoded_size)
#endif
static inline size_t device_rpc_info_response_encoded_size(const info_response_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_info_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_encode)
#endif
static inline wl_codec_status_t device_rpc_info_response_encode(const info_response_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_info_response_desc, value, out, cap, length); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_decode)
#endif
static inline wl_codec_status_t device_rpc_info_response_decode(const uint8_t *input, size_t length, info_response_t *out) { return device_rpc_wlc_decode(&device_rpc_info_response_desc, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_measure)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_measure
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_wlc_detail_measure)
#endif
static inline wl_codec_status_t device_rpc_configure_request_wlc_detail_measure(const configure_request_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_configure_request_desc, value, size); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_clear)
#endif
static inline void device_rpc_configure_request_clear(configure_request_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_configure_request_desc, value); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_encoded_size)
#endif
static inline size_t device_rpc_configure_request_encoded_size(const configure_request_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_configure_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_encode(const configure_request_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_configure_request_desc, value, out, cap, length); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_decode(const uint8_t *input, size_t length, configure_request_t *out) { return device_rpc_wlc_decode(&device_rpc_configure_request_desc, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_wlc_detail_measure)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_wlc_detail_measure
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_wlc_detail_measure)
#endif
static inline wl_codec_status_t device_rpc_configure_response_wlc_detail_measure(const configure_response_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_configure_response_desc, value, size); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_clear)
#endif
static inline void device_rpc_configure_response_clear(configure_response_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_configure_response_desc, value); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_encoded_size)
#endif
static inline size_t device_rpc_configure_response_encoded_size(const configure_response_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_configure_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_encode(const configure_response_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_configure_response_desc, value, out, cap, length); }
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_decode(const uint8_t *input, size_t length, configure_response_t *out) { return device_rpc_wlc_decode(&device_rpc_configure_response_desc, input, length, out); }

/* A descriptor embeds the ordinary wire plan as its first member. Owned
 * lengths/data use actual member offsets; no view struct aliases an owned one. */
typedef struct {
  device_rpc_wlc_desc_t wire;
  size_t size;
  void (*defaults)(void *);
} device_rpc_wlc_value_desc_t;
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_wlc_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_value_decode)
#endif
static inline wl_codec_status_t device_rpc_wlc_value_decode(const device_rpc_wlc_desc_t *, const uint8_t *, size_t, void *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_wlc_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_value_clear)
#endif
static void device_rpc_wlc_value_clear(const device_rpc_wlc_desc_t *wire, void *out) {
  const device_rpc_wlc_value_desc_t *d = (const device_rpc_wlc_value_desc_t *)wire;
  memset(out, 0, d->size);
  d->defaults(out);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_value_read)
DEVICE_RPC_LAYOUT_device_rpc_wlc_value_read
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_value_read)
#endif
static wl_codec_status_t device_rpc_wlc_value_read(const device_rpc_wlc_field_t *f, const uint8_t *in,
    size_t n, size_t *at, void *out) {
  if (f->kind == DEVICE_RPC_WLC_BYTES || f->kind == DEVICE_RPC_WLC_STRING || f->kind == DEVICE_RPC_WLC_MESSAGE) {
    uint64_t length;
    wl_codec_status_t status = device_rpc_wlc_getv(in, n, at, &length);
    if (status != WL_CODEC_OK) return status;
    if ((f->kind == DEVICE_RPC_WLC_BYTES || f->kind == DEVICE_RPC_WLC_STRING) &&
        f->max_length != 0U && length > f->max_length) return WL_CODEC_ERR_INVALID_VALUE;
    if (length > n - *at) return WL_CODEC_ERR_MALFORMED;
    size_t bytes = (size_t)length;
    if (f->kind == DEVICE_RPC_WLC_MESSAGE) {
      status = device_rpc_wlc_value_decode(f->nested, in + *at, bytes, out);
      if (status != WL_CODEC_OK) return status;
    } else
    {
      if (f->kind == DEVICE_RPC_WLC_STRING && !device_rpc_wlc_utf8(in + *at, bytes)) return WL_CODEC_ERR_UTF8;
      uint8_t *data = (uint8_t *)out + f->capacity;
      *(size_t *)out = bytes;
      if (f->string_default != NULL) memset(data, 0, (size_t)f->max_length + 1U);
      if (bytes != 0U) memcpy(data, in + *at, bytes);
      if (f->kind == DEVICE_RPC_WLC_STRING) data[bytes] = 0U;
    }
    *at += bytes;
    return WL_CODEC_OK;
  }
  return device_rpc_wlc_read_value(f, in, n, at, out);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_wlc_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_wlc_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_wlc_value_decode)
#endif
static inline wl_codec_status_t device_rpc_wlc_value_decode(const device_rpc_wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  device_rpc_wlc_value_clear(d, out);
  // A hint only: missing, unknown or reordered fields still use exact lookup.
  // Each recursive decode owns its cursor; empty schemas avoid NULL arithmetic.
  const device_rpc_wlc_field_t *next = d->fields;
  const device_rpc_wlc_field_t *end = d->count == 0U ? next : next + d->count;
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = device_rpc_wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t raw_number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    uint16_t number = (uint16_t)raw_number;
    const device_rpc_wlc_field_t *f = next != end && next->number == number
                               ? next : device_rpc_wlc_find_field(d, number);
    if (f == NULL) {
      if ((s = device_rpc_wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != device_rpc_wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
    next = f->card == DEVICE_RPC_WLC_REPEATED ? f : f + 1;
    uint8_t *base = out;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
      if ((s = device_rpc_wlc_read_packed(f, in, n, &at, base + f->value)) != WL_CODEC_OK)
        return s;
      *(bool *)(base + f->has) = true;
      continue;
    }
    void *p = base + f->value;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = device_rpc_wlc_value_read(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static const device_rpc_wlc_value_desc_t device_rpc_info_request_value_desc;
static const device_rpc_wlc_value_desc_t device_rpc_settings_value_desc;
static const device_rpc_wlc_value_desc_t device_rpc_info_response_value_desc;
static const device_rpc_wlc_value_desc_t device_rpc_configure_request_value_desc;
static const device_rpc_wlc_value_desc_t device_rpc_configure_response_value_desc;
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_defaults)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_defaults)
#endif
static void device_rpc_info_request_value_defaults(info_request_value_t *out) {
  (void)out;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_decode_defaults)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_decode_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_decode_defaults)
#endif
static void device_rpc_info_request_value_decode_defaults(void *out) { device_rpc_info_request_value_defaults(out); }

static const device_rpc_wlc_value_desc_t device_rpc_info_request_value_desc = { { NULL, 0U, DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(info_request_value_t), device_rpc_info_request_value_decode_defaults };

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_borrow)
#endif
static wl_codec_status_t device_rpc_info_request_value_borrow(const info_request_value_t *, info_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_wlc_detail_value_decode)
#endif
static inline wl_codec_status_t device_rpc_info_request_wlc_detail_value_decode(const uint8_t *input, size_t length, info_request_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_info_request_value_desc.wire, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_copy_fields)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_copy_fields
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_copy_fields)
#endif
static void device_rpc_info_request_value_copy_fields(const info_request_t *view, info_request_value_t *out) {
  (void)view; (void)out;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_value_copy)
DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_value_copy
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_wlc_detail_value_copy)
#endif
static inline void device_rpc_info_request_wlc_detail_value_copy(const info_request_t *view, info_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_info_request_value_copy_fields(view, out);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_clear)
#endif
static inline void device_rpc_info_request_value_clear(info_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_info_request_value_defaults(value);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_from_view(const info_request_t *view, info_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_info_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_borrow)
#endif
static wl_codec_status_t device_rpc_info_request_value_borrow(const info_request_value_t *value, info_request_t *out) {
  device_rpc_info_request_clear(out);
  (void)value;
  out->_empty = 0U;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_to_view(const info_request_value_t *value, info_request_t *out) {
  info_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_info_request_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_encoded_size)
#endif
static inline size_t device_rpc_info_request_value_encoded_size(const info_request_value_t *value) {
  info_request_t view;
  if (value == NULL || device_rpc_info_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_info_request_encoded_size(&view);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_encode)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_encode(const info_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  info_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_info_request_encode(&view, out, capacity, length) : status;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_request_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_value_decode)
#endif
static inline wl_codec_status_t device_rpc_info_request_value_decode(const uint8_t *input, size_t length, info_request_value_t *out) {
  info_request_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_defaults)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_defaults)
#endif
static void device_rpc_settings_value_defaults(settings_value_t *out) {
  (void)out;
  out->label.length = 6U;
  memcpy(out->label.data, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", 6U);
  out->enabled = 0;
  out->mode = INT32_C(1);
  out->floor = INT64_MIN;
  out->ceiling = UINT64_C(18446744073709551615);
  out->timeout = 0;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_decode_defaults)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_decode_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_decode_defaults)
#endif
static void device_rpc_settings_value_decode_defaults(void *out) { device_rpc_settings_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_settings_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_value_t, label), offsetof(settings_value_t, has_label), 0, offsetof(settings_value_t, label.data) - offsetof(settings_value_t, label), sizeof(wl_codec_string_t), 0U, 12U, { 10U, 0U, 0U }, 0, 6ULL, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BOOL, 1, 0U, 1U, offsetof(settings_value_t, enabled), offsetof(settings_value_t, has_enabled), 0, 0, sizeof(bool), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_ENUM, 0, 0U, 1U, offsetof(settings_value_t, mode), offsetof(settings_value_t, has_mode), 0, 0, sizeof(mode_t), 0U, 0U, { 24U, 0U, 0U }, INT32_C(1), 0ULL, NULL, NULL },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 1, 2U, 1U, offsetof(settings_value_t, token), offsetof(settings_value_t, has_token), 0, offsetof(settings_value_t, token.data) - offsetof(settings_value_t, token), sizeof(wl_codec_bytes_t), 0U, 8U, { 34U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_FLOAT32, 1, 2U, 1U, offsetof(settings_value_t, gains), offsetof(settings_value_t, has_gains), 0, 0, sizeof(float), 3U, 0U, { 42U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 6U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_F64, 0, 2U, 1U, offsetof(settings_value_t, counters), offsetof(settings_value_t, has_counters), 0, 0, sizeof(uint64_t), 2U, 0U, { 50U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 7U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_I64, 0, 0U, 1U, offsetof(settings_value_t, floor), offsetof(settings_value_t, has_floor), 0, 0, sizeof(int64_t), 0U, 0U, { 56U, 0U, 0U }, INT64_MIN, 0ULL, NULL, NULL },
  { 8U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U64, 0, 0U, 1U, offsetof(settings_value_t, ceiling), offsetof(settings_value_t, has_ceiling), 0, 0, sizeof(uint64_t), 0U, 0U, { 64U, 0U, 0U }, 0, 18446744073709551615ULL, NULL, NULL },
  { 9U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_value_t, class_), offsetof(settings_value_t, has_class_), 0, offsetof(settings_value_t, class_.data) - offsetof(settings_value_t, class_), sizeof(wl_codec_string_t), 0U, 8U, { 74U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 10U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U16, 0, 0U, 1U, offsetof(settings_value_t, timeout), offsetof(settings_value_t, has_timeout), 0, 0, sizeof(uint16_t), 0U, 0U, { 80U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_value_desc_t device_rpc_settings_value_desc = { { device_rpc_settings_value_fields, sizeof(device_rpc_settings_value_fields) / sizeof(device_rpc_settings_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_DENSE }, sizeof(settings_value_t), device_rpc_settings_value_decode_defaults };

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_borrow)
#endif
static wl_codec_status_t device_rpc_settings_value_borrow(const settings_value_t *, settings_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_wlc_detail_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_settings_wlc_detail_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_wlc_detail_value_decode)
#endif
static inline wl_codec_status_t device_rpc_settings_wlc_detail_value_decode(const uint8_t *input, size_t length, settings_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_settings_value_desc.wire, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_copy_fields)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_copy_fields
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_copy_fields)
#endif
static void device_rpc_settings_value_copy_fields(const settings_t *view, settings_value_t *out) {
  out->has_label = view->has_label;
  if (view->has_label) {
    out->label.length = view->label.length;
    if (view->label.length != 0U) memcpy(out->label.data, view->label.data, view->label.length);
  } else {
  out->label.length = 6U;
  memcpy(out->label.data, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", 6U);
  }
  out->has_enabled = view->has_enabled;
  out->enabled = view->has_enabled ? view->enabled : 0;
  out->has_mode = view->has_mode;
  out->mode = view->has_mode ? view->mode : INT32_C(1);
  out->has_token = view->has_token;
  if (view->has_token) {
    out->token.length = view->token.length;
    if (view->token.length != 0U) memcpy(out->token.data, view->token.data, view->token.length);
  } else {
  }
  out->has_gains = view->has_gains;
  if (view->has_gains) memcpy(out->gains, view->gains, sizeof(out->gains));
  out->has_counters = view->has_counters;
  if (view->has_counters) memcpy(out->counters, view->counters, sizeof(out->counters));
  out->has_floor = view->has_floor;
  out->floor = view->has_floor ? view->floor : INT64_MIN;
  out->has_ceiling = view->has_ceiling;
  out->ceiling = view->has_ceiling ? view->ceiling : UINT64_C(18446744073709551615);
  out->has_class_ = view->has_class_;
  if (view->has_class_) {
    out->class_.length = view->class_.length;
    if (view->class_.length != 0U) memcpy(out->class_.data, view->class_.data, view->class_.length);
  } else {
  }
  out->has_timeout = view->has_timeout;
  out->timeout = view->has_timeout ? view->timeout : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_wlc_detail_value_copy)
DEVICE_RPC_LAYOUT_device_rpc_settings_wlc_detail_value_copy
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_wlc_detail_value_copy)
#endif
static inline void device_rpc_settings_wlc_detail_value_copy(const settings_t *view, settings_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_settings_value_copy_fields(view, out);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_clear)
#endif
static inline void device_rpc_settings_value_clear(settings_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_settings_value_defaults(value);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_settings_value_from_view(const settings_t *view, settings_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_settings_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_borrow)
#endif
static wl_codec_status_t device_rpc_settings_value_borrow(const settings_value_t *value, settings_t *out) {
  device_rpc_settings_clear(out);
  out->has_label = value->has_label;
  if (value->has_label) {
    if (value->label.length > 12U) return WL_CODEC_ERR_INVALID_VALUE;
    out->label.length = value->label.length;
    out->label.data = value->label.data;
  }
  out->has_enabled = value->has_enabled;
  if (value->has_enabled) {
    out->enabled = value->enabled;
  }
  out->has_mode = value->has_mode;
  if (value->has_mode) {
    out->mode = value->mode;
  }
  out->has_token = value->has_token;
  if (value->has_token) {
    if (value->token.length > 8U) return WL_CODEC_ERR_INVALID_VALUE;
    out->token.length = value->token.length;
    out->token.data = value->token.data;
  }
  out->has_gains = value->has_gains;
  if (value->has_gains) {
    memcpy(out->gains, value->gains, sizeof(out->gains));
  }
  out->has_counters = value->has_counters;
  if (value->has_counters) {
    memcpy(out->counters, value->counters, sizeof(out->counters));
  }
  out->has_floor = value->has_floor;
  if (value->has_floor) {
    out->floor = value->floor;
  }
  out->has_ceiling = value->has_ceiling;
  if (value->has_ceiling) {
    out->ceiling = value->ceiling;
  }
  out->has_class_ = value->has_class_;
  if (value->has_class_) {
    if (value->class_.length > 8U) return WL_CODEC_ERR_INVALID_VALUE;
    out->class_.length = value->class_.length;
    out->class_.data = value->class_.data;
  }
  out->has_timeout = value->has_timeout;
  if (value->has_timeout) {
    out->timeout = value->timeout;
  }
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_settings_value_to_view(const settings_value_t *value, settings_t *out) {
  settings_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_settings_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_encoded_size)
#endif
static inline size_t device_rpc_settings_value_encoded_size(const settings_value_t *value) {
  settings_t view;
  if (value == NULL || device_rpc_settings_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_settings_encoded_size(&view);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_encode)
#endif
static inline wl_codec_status_t device_rpc_settings_value_encode(const settings_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  settings_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_settings_encode(&view, out, capacity, length) : status;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_settings_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_settings_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_settings_value_decode)
#endif
static inline wl_codec_status_t device_rpc_settings_value_decode(const uint8_t *input, size_t length, settings_value_t *out) {
  settings_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_defaults)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_defaults)
#endif
static void device_rpc_info_response_value_defaults(info_response_value_t *out) {
  (void)out;
  device_rpc_settings_value_defaults(&out->settings);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_decode_defaults)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_decode_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_decode_defaults)
#endif
static void device_rpc_info_response_value_decode_defaults(void *out) { device_rpc_info_response_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_info_response_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 1, 2U, 1U, offsetof(info_response_value_t, name), offsetof(info_response_value_t, has_name), 0, offsetof(info_response_value_t, name.data) - offsetof(info_response_value_t, name), sizeof(wl_codec_string_t), 0U, 32U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(info_response_value_t, settings), offsetof(info_response_value_t, has_settings), 0, 0, sizeof(settings_value_t), 0U, 0U, { 18U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
};
static const device_rpc_wlc_value_desc_t device_rpc_info_response_value_desc = { { device_rpc_info_response_value_fields, sizeof(device_rpc_info_response_value_fields) / sizeof(device_rpc_info_response_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(info_response_value_t), device_rpc_info_response_value_decode_defaults };

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_borrow)
#endif
static wl_codec_status_t device_rpc_info_response_value_borrow(const info_response_value_t *, info_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_wlc_detail_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_wlc_detail_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_wlc_detail_value_decode)
#endif
static inline wl_codec_status_t device_rpc_info_response_wlc_detail_value_decode(const uint8_t *input, size_t length, info_response_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_info_response_value_desc.wire, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_copy_fields)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_copy_fields
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_copy_fields)
#endif
static void device_rpc_info_response_value_copy_fields(const info_response_t *view, info_response_value_t *out) {
  out->has_name = view->has_name;
  if (view->has_name) {
    out->name.length = view->name.length;
    if (view->name.length != 0U) memcpy(out->name.data, view->name.data, view->name.length);
  } else {
  }
  out->has_settings = view->has_settings;
  if (view->has_settings) device_rpc_settings_value_copy_fields(&view->settings, &out->settings);
  else device_rpc_settings_value_defaults(&out->settings);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_wlc_detail_value_copy)
DEVICE_RPC_LAYOUT_device_rpc_info_response_wlc_detail_value_copy
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_wlc_detail_value_copy)
#endif
static inline void device_rpc_info_response_wlc_detail_value_copy(const info_response_t *view, info_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_info_response_value_copy_fields(view, out);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_clear)
#endif
static inline void device_rpc_info_response_value_clear(info_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_info_response_value_defaults(value);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_from_view(const info_response_t *view, info_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_info_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_borrow)
#endif
static wl_codec_status_t device_rpc_info_response_value_borrow(const info_response_value_t *value, info_response_t *out) {
  device_rpc_info_response_clear(out);
  out->has_name = value->has_name;
  if (value->has_name) {
    if (value->name.length > 32U) return WL_CODEC_ERR_INVALID_VALUE;
    out->name.length = value->name.length;
    out->name.data = value->name.data;
  }
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->settings, &out->settings);
    if (status != WL_CODEC_OK) return status;
  }
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_to_view(const info_response_value_t *value, info_response_t *out) {
  info_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_info_response_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_encoded_size)
#endif
static inline size_t device_rpc_info_response_value_encoded_size(const info_response_value_t *value) {
  info_response_t view;
  if (value == NULL || device_rpc_info_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_info_response_encoded_size(&view);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_encode)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_encode(const info_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  info_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_info_response_encode(&view, out, capacity, length) : status;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_response_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_info_response_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_response_value_decode)
#endif
static inline wl_codec_status_t device_rpc_info_response_value_decode(const uint8_t *input, size_t length, info_response_value_t *out) {
  info_response_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_defaults)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_defaults)
#endif
static void device_rpc_configure_request_value_defaults(configure_request_value_t *out) {
  (void)out;
  device_rpc_settings_value_defaults(&out->settings);
  device_rpc_settings_value_defaults(&out->backup);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_decode_defaults)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_decode_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_decode_defaults)
#endif
static void device_rpc_configure_request_value_decode_defaults(void *out) { device_rpc_configure_request_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_configure_request_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_request_value_t, settings), offsetof(configure_request_value_t, has_settings), 0, 0, sizeof(settings_value_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_request_value_t, opaque), offsetof(configure_request_value_t, has_opaque), 0, offsetof(configure_request_value_t, opaque.data) - offsetof(configure_request_value_t, opaque), sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_request_value_t, backup), offsetof(configure_request_value_t, has_backup), 0, 0, sizeof(settings_value_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
};
static const device_rpc_wlc_value_desc_t device_rpc_configure_request_value_desc = { { device_rpc_configure_request_value_fields, sizeof(device_rpc_configure_request_value_fields) / sizeof(device_rpc_configure_request_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(configure_request_value_t), device_rpc_configure_request_value_decode_defaults };

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_borrow)
#endif
static wl_codec_status_t device_rpc_configure_request_value_borrow(const configure_request_value_t *, configure_request_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_wlc_detail_value_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_wlc_detail_value_decode(const uint8_t *input, size_t length, configure_request_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_configure_request_value_desc.wire, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_copy_fields)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_copy_fields
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_copy_fields)
#endif
static void device_rpc_configure_request_value_copy_fields(const configure_request_t *view, configure_request_value_t *out) {
  out->has_settings = view->has_settings;
  if (view->has_settings) device_rpc_settings_value_copy_fields(&view->settings, &out->settings);
  else device_rpc_settings_value_defaults(&out->settings);
  out->has_opaque = view->has_opaque;
  if (view->has_opaque) {
    out->opaque.length = view->opaque.length;
    if (view->opaque.length != 0U) memcpy(out->opaque.data, view->opaque.data, view->opaque.length);
  } else {
  }
  out->has_backup = view->has_backup;
  if (view->has_backup) device_rpc_settings_value_copy_fields(&view->backup, &out->backup);
  else device_rpc_settings_value_defaults(&out->backup);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_value_copy)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_value_copy
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_wlc_detail_value_copy)
#endif
static inline void device_rpc_configure_request_wlc_detail_value_copy(const configure_request_t *view, configure_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_configure_request_value_copy_fields(view, out);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_clear)
#endif
static inline void device_rpc_configure_request_value_clear(configure_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_configure_request_value_defaults(value);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_from_view(const configure_request_t *view, configure_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_configure_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_borrow)
#endif
static wl_codec_status_t device_rpc_configure_request_value_borrow(const configure_request_value_t *value, configure_request_t *out) {
  device_rpc_configure_request_clear(out);
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->settings, &out->settings);
    if (status != WL_CODEC_OK) return status;
  }
  out->has_opaque = value->has_opaque;
  if (value->has_opaque) {
    if (value->opaque.length > 4U) return WL_CODEC_ERR_INVALID_VALUE;
    out->opaque.length = value->opaque.length;
    out->opaque.data = value->opaque.data;
  }
  out->has_backup = value->has_backup;
  if (value->has_backup) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->backup, &out->backup);
    if (status != WL_CODEC_OK) return status;
  }
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_to_view(const configure_request_value_t *value, configure_request_t *out) {
  configure_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_configure_request_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_encoded_size)
#endif
static inline size_t device_rpc_configure_request_value_encoded_size(const configure_request_value_t *value) {
  configure_request_t view;
  if (value == NULL || device_rpc_configure_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_configure_request_encoded_size(&view);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_encode(const configure_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  configure_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_configure_request_encode(&view, out, capacity, length) : status;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_value_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_request_value_decode(const uint8_t *input, size_t length, configure_request_value_t *out) {
  configure_request_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_defaults)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_defaults)
#endif
static void device_rpc_configure_response_value_defaults(configure_response_value_t *out) {
  (void)out;
  device_rpc_settings_value_defaults(&out->settings);
  device_rpc_settings_value_defaults(&out->backup);
  out->count = UINT32_C(0);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_decode_defaults)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_decode_defaults
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_decode_defaults)
#endif
static void device_rpc_configure_response_value_decode_defaults(void *out) { device_rpc_configure_response_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_configure_response_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_response_value_t, settings), offsetof(configure_response_value_t, has_settings), 0, 0, sizeof(settings_value_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_response_value_t, opaque), offsetof(configure_response_value_t, has_opaque), 0, offsetof(configure_response_value_t, opaque.data) - offsetof(configure_response_value_t, opaque), sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_response_value_t, backup), offsetof(configure_response_value_t, has_backup), 0, 0, sizeof(settings_value_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U32, 1, 0U, 1U, offsetof(configure_response_value_t, count), offsetof(configure_response_value_t, has_count), 0, 0, sizeof(uint32_t), 0U, 0U, { 32U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_value_desc_t device_rpc_configure_response_value_desc = { { device_rpc_configure_response_value_fields, sizeof(device_rpc_configure_response_value_fields) / sizeof(device_rpc_configure_response_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(configure_response_value_t), device_rpc_configure_response_value_decode_defaults };

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_borrow)
#endif
static wl_codec_status_t device_rpc_configure_response_value_borrow(const configure_response_value_t *, configure_response_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_wlc_detail_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_wlc_detail_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_wlc_detail_value_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_wlc_detail_value_decode(const uint8_t *input, size_t length, configure_response_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_configure_response_value_desc.wire, input, length, out); }

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_copy_fields)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_copy_fields
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_copy_fields)
#endif
static void device_rpc_configure_response_value_copy_fields(const configure_response_t *view, configure_response_value_t *out) {
  out->has_settings = view->has_settings;
  if (view->has_settings) device_rpc_settings_value_copy_fields(&view->settings, &out->settings);
  else device_rpc_settings_value_defaults(&out->settings);
  out->has_opaque = view->has_opaque;
  if (view->has_opaque) {
    out->opaque.length = view->opaque.length;
    if (view->opaque.length != 0U) memcpy(out->opaque.data, view->opaque.data, view->opaque.length);
  } else {
  }
  out->has_backup = view->has_backup;
  if (view->has_backup) device_rpc_settings_value_copy_fields(&view->backup, &out->backup);
  else device_rpc_settings_value_defaults(&out->backup);
  out->has_count = view->has_count;
  out->count = view->has_count ? view->count : UINT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_wlc_detail_value_copy)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_wlc_detail_value_copy
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_wlc_detail_value_copy)
#endif
static inline void device_rpc_configure_response_wlc_detail_value_copy(const configure_response_t *view, configure_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_configure_response_value_copy_fields(view, out);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_clear)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_clear
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_clear)
#endif
static inline void device_rpc_configure_response_value_clear(configure_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_configure_response_value_defaults(value);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_from_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_from_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_from_view)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_from_view(const configure_response_t *view, configure_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_configure_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_borrow)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_borrow
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_borrow)
#endif
static wl_codec_status_t device_rpc_configure_response_value_borrow(const configure_response_value_t *value, configure_response_t *out) {
  device_rpc_configure_response_clear(out);
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->settings, &out->settings);
    if (status != WL_CODEC_OK) return status;
  }
  out->has_opaque = value->has_opaque;
  if (value->has_opaque) {
    if (value->opaque.length > 4U) return WL_CODEC_ERR_INVALID_VALUE;
    out->opaque.length = value->opaque.length;
    out->opaque.data = value->opaque.data;
  }
  out->has_backup = value->has_backup;
  if (value->has_backup) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->backup, &out->backup);
    if (status != WL_CODEC_OK) return status;
  }
  out->has_count = value->has_count;
  if (value->has_count) {
    out->count = value->count;
  }
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_to_view)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_to_view
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_to_view)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_to_view(const configure_response_value_t *value, configure_response_t *out) {
  configure_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_configure_response_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encoded_size)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encoded_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_encoded_size)
#endif
static inline size_t device_rpc_configure_response_value_encoded_size(const configure_response_value_t *value) {
  configure_response_t view;
  if (value == NULL || device_rpc_configure_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_configure_response_encoded_size(&view);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_encode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_encode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_encode(const configure_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  configure_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_configure_response_encode(&view, out, capacity, length) : status;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_decode)
DEVICE_RPC_LAYOUT_device_rpc_configure_response_value_decode
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_response_value_decode)
#endif
static inline wl_codec_status_t device_rpc_configure_response_value_decode(const uint8_t *input, size_t length, configure_response_value_t *out) {
  configure_response_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

wl_codec_status_t info_request_wlc_detail_fingerprint(const info_request_t *, uint64_t *, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_fingerprint)
DEVICE_RPC_LAYOUT_device_rpc_info_request_wlc_detail_fingerprint
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_info_request_wlc_detail_fingerprint)
#endif
static inline wl_codec_status_t device_rpc_info_request_wlc_detail_fingerprint(const info_request_t *value, uint64_t *hash, size_t *length) { return info_request_wlc_detail_fingerprint(value, hash, length); }

wl_codec_status_t configure_request_wlc_detail_fingerprint(const configure_request_t *, uint64_t *, size_t *);
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_fingerprint)
DEVICE_RPC_LAYOUT_device_rpc_configure_request_wlc_detail_fingerprint
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_configure_request_wlc_detail_fingerprint)
#endif
static inline wl_codec_status_t device_rpc_configure_request_wlc_detail_fingerprint(const configure_request_t *value, uint64_t *hash, size_t *length) { return configure_request_wlc_detail_fingerprint(value, hash, length); }

/* Managed RPC v4: BE64 originating client session, then tagged varuint32
 * correlation. Its first byte reserves bit zero for rejection; the remaining
 * bits encode all 32 operation bits. Native message ID determines direction.
 * Only rejection adds BE32 nonzero status. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_read_u32)
DEVICE_RPC_LAYOUT_device_rpc_read_u32
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_read_u32)
#endif
static uint32_t device_rpc_read_u32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
         ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_write_u32)
DEVICE_RPC_LAYOUT_device_rpc_write_u32
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_write_u32)
#endif
static void device_rpc_write_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)(value >> 24U);
  data[1] = (uint8_t)(value >> 16U);
  data[2] = (uint8_t)(value >> 8U);
  data[3] = (uint8_t)value;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_header_size)
DEVICE_RPC_LAYOUT_device_rpc_header_size
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_header_size)
#endif
static size_t device_rpc_header_size(uint32_t operation_id, int32_t status) {
  size_t size = status == 0 ? 9U : 13U;
  operation_id >>= 6U;
  while (operation_id != 0U) { ++size; operation_id >>= 7U; }
  return size;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_header_read)
DEVICE_RPC_LAYOUT_device_rpc_header_read
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_header_read)
#endif
static wl_rpc_err_t device_rpc_header_read(const uint8_t *data, size_t length,
    uint8_t kind, uint32_t *operation_id, int32_t *status, uint64_t *session,
    size_t *header_length) {
  uint32_t operation;
  size_t at = 9U;
  uint8_t byte;
  bool rejection;
  if (data == NULL || length < 9U) return WL_RPC_ERR_MALFORMED_METADATA;
  *session = ((uint64_t)device_rpc_read_u32(data) << 32U) |
      device_rpc_read_u32(data + 4U);
  byte = data[8U];
  rejection = (byte & 1U) != 0U;
  operation = (uint32_t)((byte >> 1U) & 63U);
  for (unsigned shift = 6U; (byte & 128U) != 0U; shift += 7U) {
    if (at == length || shift > 27U) return WL_RPC_ERR_MALFORMED_METADATA;
    byte = data[at++];
    if ((shift == 27U && byte > 31U) || byte == 0U)
      return WL_RPC_ERR_MALFORMED_METADATA;
    operation |= (uint32_t)(byte & 127U) << shift;
  }
  *operation_id = operation;
  *status = 0;
  if (*session == 0U || operation == 0U || (rejection && kind == 1U))
    return WL_RPC_ERR_MALFORMED_METADATA;
  if (rejection) {
    uint32_t bits;
    if (length != at + 4U) return WL_RPC_ERR_MALFORMED_METADATA;
    bits = device_rpc_read_u32(data + at);
    *status = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
    if (*status == 0) return WL_RPC_ERR_MALFORMED_METADATA;
    at += 4U;
  }
  *header_length = at;
  return WL_RPC_OK;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_header_write)
DEVICE_RPC_LAYOUT_device_rpc_header_write
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_header_write)
#endif
static size_t device_rpc_header_write(uint8_t *data, uint8_t kind,
    uint32_t operation_id, int32_t status, uint64_t session) {
  size_t at = 9U;
  (void)kind;
  device_rpc_write_u32(data, (uint32_t)(session >> 32U));
  device_rpc_write_u32(data + 4U, (uint32_t)session);
  data[8U] = (uint8_t)(((operation_id & 63U) << 1U) | (status != 0 ? 1U : 0U));
  operation_id >>= 6U;
  if (operation_id != 0U) data[8U] |= 128U;
  while (operation_id != 0U) {
    uint8_t byte = (uint8_t)(operation_id & 127U);
    operation_id >>= 7U;
    data[at++] = operation_id != 0U ? (uint8_t)(byte | 128U) : byte;
  }
  if (status != 0) { device_rpc_write_u32(data + at, (uint32_t)status); at += 4U; }
  return at;
}
/* Shared managed-RPC control flow. Typed adapters keep codec calls type-safe;
 * no function-pointer casts, heap, or extra persistent scratch are needed. */
typedef wl_codec_status_t (*device_rpc_response_encoder_fn)(const void *,
    uint8_t *, size_t, size_t *, bool);

#if DEVICE_RUNTIME_HAS_RPC_SERVER
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_request_prepare)
DEVICE_RPC_LAYOUT_device_rpc_request_prepare
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_request_prepare)
#endif
static bool device_rpc_request_prepare(const wl_event_t *event,
    device_runtime_t *runtime, wl_event_type_t expected,
    device_runtime_result_t *result, uint64_t *session, size_t *header_length) {
  int32_t status = 0;
  result->detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (event->type != expected) {
    result->domain = DEVICE_RUNTIME_DELIVERY_MISMATCH;
    return false;
  }
  if (runtime->rpc_server == NULL) {
    result->domain = DEVICE_RUNTIME_MISSING_ROUTE;
    return false;
  }
  result->detail.rpc.rpc_result = device_rpc_header_read(event->payload,
      event->payload_len, 1U, &result->detail.rpc.operation_id, &status, session, header_length);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return false;
  }
  if (event->type == WL_EVT_RELIABLE_RX && event->peer_session_id != *session) {
    result->detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return false;
  }
  return true;
}

/* Return true only for a new request. Peer observation must remain after
 * successful decoding/fingerprinting: malformed input cannot cancel a peer. */
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_request_begin)
DEVICE_RPC_LAYOUT_device_rpc_request_begin
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_request_begin)
#endif
static bool device_rpc_request_begin(wl_ctx_t *ctx, device_runtime_t *runtime,
    const wl_rpc_request_identity_t *identity, size_t canonical_length,
    wl_time_ms_t now_ms, device_runtime_result_t *result) {
  wl_rpc_server_request_t request = {0};
  wl_rpc_server_response_t replay = {0};
  if (runtime->rpc_peer.session_id != identity->peer_session_id) {
    wl_rpc_peer_observation_t observation = {0};
    result->detail.rpc.rpc_result = device_runtime_peer_observe(ctx, runtime,
        identity->peer_session_id, &observation);
    if (result->detail.rpc.rpc_result != WL_RPC_OK) {
      result->domain = DEVICE_RUNTIME_RPC_ERROR;
      return false;
    }
    result->detail.rpc.peer_changed = observation.changed;
  }
  (void)canonical_length; /* Fingerprinting uses the unchanged public canonical codec. */
  result->detail.rpc.rpc_result = wl_rpc_server_begin(runtime->rpc_server,
      identity, now_ms, &result->detail.rpc.rpc_disposition, &request, &replay);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return false;
  }
  switch (result->detail.rpc.rpc_disposition) {
    case WL_RPC_SERVER_NEW:
      result->detail.rpc.server_request = request;
      return true;
    case WL_RPC_SERVER_PENDING_DUPLICATE:
      result->domain = DEVICE_RUNTIME_OK;
      break;
    case WL_RPC_SERVER_REPLAY:
      result->detail.rpc.server_response = replay;
      result->detail.rpc.application_result = replay.application_status;
      result->detail.rpc.payload_length = replay.response_length;
      result->domain = DEVICE_RUNTIME_OK;
      break;
    case WL_RPC_SERVER_CONFLICT:
      result->detail.rpc.rpc_result = WL_RPC_ERR_OPERATION_CONFLICT;
      result->domain = DEVICE_RUNTIME_RPC_ERROR;
      break;
    default:
      result->detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
      result->domain = DEVICE_RUNTIME_RPC_ERROR;
      break;
  }
  return false;
}

#endif

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_finish_response)
DEVICE_RPC_LAYOUT_device_rpc_finish_response
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_finish_response)
#endif
static void device_rpc_finish_response(device_runtime_t *runtime,
    const wl_rpc_server_request_t *request, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned,
    device_rpc_response_encoder_fn encode, device_runtime_result_t *result) {
  wl_rpc_server_response_buffer_t buffer = {0};
  wl_rpc_server_response_t cached = {0};
  size_t encoded_length = 0U;
  const size_t header_length = device_rpc_header_size(request->identity.operation_id, application_status);
  result->detail.rpc.operation_id = request->identity.operation_id;
  result->detail.rpc.rpc_result = wl_rpc_server_response_prepare(runtime->rpc_server, request, &buffer);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return;
  }
  if (buffer.capacity < header_length) {
    result->detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_TOO_LARGE;
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return;
  }
  device_rpc_header_write(buffer.data, 2U, request->identity.operation_id,
      application_status, request->identity.peer_session_id);
  if (application_status == 0) {
    result->detail.rpc.codec_status = encode(response, buffer.data + header_length,
        buffer.capacity - header_length, &encoded_length, owned);
    if (result->detail.rpc.codec_status != WL_CODEC_OK) {
      result->domain = DEVICE_RUNTIME_CODEC_ERROR;
      return;
    }
  }
  result->detail.rpc.rpc_result = wl_rpc_server_response_commit(runtime->rpc_server,
      &buffer, application_status, encoded_length + header_length, now_ms, &cached);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return;
  }
  result->detail.rpc.server_response = cached;
  result->detail.rpc.payload_length = cached.response_length;
  result->domain = DEVICE_RUNTIME_OK;
}
static const uint64_t device_rpc_fingerprint_seed = UINT64_C(0x24faaea3493c1c2e);
wl_codec_status_t configure_request_wlc_detail_fingerprint(const configure_request_t *, uint64_t *, size_t *);
#if CONFIGURE_REQUEST_HAS_VALUE
void configure_request_wlc_detail_value_copy(const configure_request_t *, configure_request_value_t *);
#endif

wl_codec_status_t info_request_wlc_detail_fingerprint(const info_request_t *, uint64_t *, size_t *);
#if INFO_REQUEST_HAS_VALUE
void info_request_wlc_detail_value_copy(const info_request_t *, info_request_value_t *);
#endif

typedef struct { wl_ctx_t *link; device_runtime_t *runtime; } device_peer_cancel_context_t;
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_cancel_peer_tx)
DEVICE_RPC_LAYOUT_device_runtime_cancel_peer_tx
#endif
static void device_runtime_cancel_peer_tx(void *context, wl_tx_handle_t handle) {
  device_peer_cancel_context_t *cancel = context;
  wl_tx_result_t ignored;
  if (cancel == NULL) return;
  (void)wl_tx_cancel(cancel->link, handle);
  /* A cancelled transaction need not emit a terminal event. Take it now, or
   * retain just its handle until the adapter releases physical TX storage. */
  if (wl_tx_take(cancel->link, handle, &ignored) == WL_ERR_INVALID_STATE)
    cancel->runtime->rpc_retiring_tx = handle;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_config_defaults)
DEVICE_RPC_LAYOUT_device_runtime_config_defaults
#endif
wl_err_t device_runtime_config_defaults(device_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  memset(config, 0, sizeof(*config));
  config->rpc_client_slot_count = 1U;
  config->rpc_client_next_operation_id = 1U;
  config->rpc_server_pending_slot_count = 1U;
  config->rpc_server_cache_slot_count = 1U;
  config->rpc_server_response_capacity = 229U;
  config->rpc_server_cache_policy = WL_RPC_CACHE_REJECT_NEW;
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_config_enable_client)
DEVICE_RPC_LAYOUT_device_runtime_config_enable_client
#endif
wl_err_t device_runtime_config_enable_client(device_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (!DEVICE_RUNTIME_HAS_RPC_CLIENT || config->rpc_client_slot_count == 0U) return WL_ERR_NOT_SUPPORTED;
  config->rpc_client_enabled = 1U;
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_config_enable_server)
DEVICE_RPC_LAYOUT_device_runtime_config_enable_server
#endif
wl_err_t device_runtime_config_enable_server(device_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (!DEVICE_RUNTIME_HAS_RPC_SERVER || config->rpc_server_pending_slot_count == 0U || config->rpc_server_cache_slot_count == 0U || config->rpc_server_response_capacity == 0U) return WL_ERR_NOT_SUPPORTED;
  config->rpc_server_enabled = 1U;
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_default_storage_descriptor)
DEVICE_RPC_LAYOUT_device_runtime_default_storage_descriptor
#endif
device_runtime_storage_t device_runtime_default_storage_descriptor(device_runtime_default_storage_t *storage) {
  device_runtime_storage_t descriptor = {0};
  if (storage != NULL) {
    descriptor.data = storage->bytes;
    descriptor.size = sizeof(storage->bytes);
    descriptor.rpc_results = storage->rpc_results;
    descriptor.rpc_result_count = 1U;
  }
  return descriptor;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_roles_valid)
DEVICE_RPC_LAYOUT_device_runtime_roles_valid
#endif
static int device_runtime_roles_valid(const device_runtime_config_t *config) {
  (void)config;
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_init_issue_str)
DEVICE_RPC_LAYOUT_device_runtime_init_issue_str
#endif
const char *device_runtime_init_issue_str(device_runtime_init_issue_t issue) {
  switch (issue) {
    case DEVICE_RUNTIME_INIT_OK: return "ok";
    case DEVICE_RUNTIME_INIT_NULL_ARGUMENT: return "null argument";
    case DEVICE_RUNTIME_INIT_ROLE_ENABLE: return "role enable must be zero or one";
    case DEVICE_RUNTIME_INIT_RETAINED_CAPACITY: return "retained capacity is zero";
    case DEVICE_RUNTIME_INIT_RPC_CLIENT_CAPACITY: return "RPC client capacity is zero";
    case DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY: return "RPC server capacity is zero";
    case DEVICE_RUNTIME_INIT_RPC_TIMEOUT: return "RPC timeout exceeds wrap-safe range";
    case DEVICE_RUNTIME_INIT_RPC_CACHE_POLICY: return "unknown RPC cache policy";
    case DEVICE_RUNTIME_INIT_LAYOUT_OVERFLOW: return "runtime layout size overflow";
    case DEVICE_RUNTIME_INIT_STORAGE_TOO_SMALL: return "runtime storage is too small";
    case DEVICE_RUNTIME_INIT_STORAGE_NULL: return "runtime storage data is null";
    case DEVICE_RUNTIME_INIT_STORAGE_ALIGNMENT: return "runtime storage is misaligned";
    case DEVICE_RUNTIME_INIT_STORAGE_OVERLAP: return "runtime storage overlaps the instance";
    case DEVICE_RUNTIME_INIT_COMPONENT: return "runtime component initialization failed";
    default: return "unknown runtime initialization issue";
  }
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_init_failure)
DEVICE_RPC_LAYOUT_device_runtime_init_failure
#endif
static int device_runtime_init_failure(device_runtime_init_diagnostic_t *diagnostic, device_runtime_init_issue_t issue, const char *field, size_t required, size_t provided, int result) {
  if (diagnostic != NULL) {
    diagnostic->issue = issue;
    diagnostic->field = field;
    diagnostic->required = required;
    diagnostic->provided = provided;
  }
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_result_storage_validate)
DEVICE_RPC_LAYOUT_device_runtime_result_storage_validate
#endif
static int device_runtime_result_storage_validate(const device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage, device_runtime_init_diagnostic_t *diagnostic) {
  uintptr_t address, other;
  size_t bytes;
  if (!config->rpc_client_enabled) return WL_OK;
  if (storage->rpc_result_count < config->rpc_client_slot_count) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_TOO_SMALL, "storage.rpc_result_count", config->rpc_client_slot_count, storage->rpc_result_count, WL_ERR_BUF_TOO_SMALL);
  if (storage->rpc_results == NULL) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_NULL, "storage.rpc_results", 1U, 0U, WL_ERR_INVALID_ARG);
  if (storage->rpc_result_count > SIZE_MAX / sizeof(*storage->rpc_results)) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_LAYOUT_OVERFLOW, "storage.rpc_result_count", 0U, storage->rpc_result_count, WL_ERR_INVALID_ARG);
  address = (uintptr_t)(void *)storage->rpc_results;
  if (address % _Alignof(device_rpc_result_value_t) != 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_ALIGNMENT, "storage.rpc_results", _Alignof(device_rpc_result_value_t), address % _Alignof(device_rpc_result_value_t), WL_ERR_INVALID_ARG);
  bytes = storage->rpc_result_count * sizeof(*storage->rpc_results);
  other = (uintptr_t)(const void *)instance;
  if ((address <= other && other - address < bytes) || (other < address && address - other < sizeof(*instance))) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_OVERLAP, "storage.rpc_results", bytes, 0U, WL_ERR_INVALID_ARG);
  other = (uintptr_t)storage->data;
  if (storage->size != 0U && ((address <= other && other - address < bytes) || (other < address && address - other < storage->size))) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_OVERLAP, "storage.rpc_results", bytes, 0U, WL_ERR_INVALID_ARG);
  return WL_OK;
}

typedef struct {
  uint8_t *base;
  size_t size;
  size_t offset;
} device_runtime_storage_cursor_t;

typedef struct {
  void *rpc_client_slots;
  void *rpc_server_pending_slots;
  void *rpc_server_cache_slots;
  void *rpc_server_responses;
  size_t rpc_server_responses_size;
} device_runtime_layout_t;

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_storage_region)
DEVICE_RPC_LAYOUT_device_runtime_storage_region
#endif
static inline int device_runtime_storage_region(device_runtime_storage_cursor_t *cursor, size_t alignment, size_t count, size_t element_size, void **out_data, size_t *out_size) {
  size_t aligned;
  size_t region_size;
  if (cursor == NULL || alignment == 0U || (alignment & (alignment - 1U)) != 0U) return WL_ERR_INVALID_ARG;
  if (out_data != NULL) *out_data = NULL;
  if (out_size != NULL) *out_size = 0U;
  if (count != 0U && element_size > SIZE_MAX / count) return WL_ERR_INVALID_ARG;
  region_size = count * element_size;
  if (cursor->offset > SIZE_MAX - (alignment - 1U)) return WL_ERR_INVALID_ARG;
  aligned = (cursor->offset + (alignment - 1U)) & ~(alignment - 1U);
  if (region_size > SIZE_MAX - aligned) return WL_ERR_INVALID_ARG;
  if (aligned + region_size > cursor->size) return WL_ERR_BUF_TOO_SMALL;
  if (out_data != NULL && cursor->base != NULL) *out_data = cursor->base + aligned;
  if (out_size != NULL) *out_size = region_size;
  cursor->offset = aligned + region_size;
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_layout)
DEVICE_RPC_LAYOUT_device_runtime_layout
#endif
static int device_runtime_layout(const device_runtime_config_t *config, uint8_t *base, size_t size, device_runtime_layout_t *out_layout, device_runtime_requirements_t *out_requirements) {
  device_runtime_storage_cursor_t cursor = {base, size, 0U};
  size_t alignment = 1U;
  int result;
  if (out_layout != NULL) memset(out_layout, 0, sizeof(*out_layout));
  if (out_requirements != NULL) memset(out_requirements, 0, sizeof(*out_requirements));
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (device_runtime_roles_valid(config) != WL_OK) return WL_ERR_NOT_SUPPORTED;
  if (config->rpc_client_enabled > 1U || config->rpc_server_enabled > 1U) return WL_ERR_INVALID_ARG;
  if (config->rpc_client_enabled != 0U) {
    if (config->rpc_client_slot_count == 0U) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_client_slot_t)) alignment = _Alignof(wl_rpc_client_slot_t);
    result = device_runtime_storage_region(&cursor, _Alignof(wl_rpc_client_slot_t), config->rpc_client_slot_count, sizeof(wl_rpc_client_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_client_slots, NULL);
    if (result != WL_OK) return result;
  }
  if (config->rpc_server_enabled != 0U) {
    if (config->rpc_server_pending_slot_count == 0U || config->rpc_server_cache_slot_count == 0U || config->rpc_server_response_capacity == 0U) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_pending_timeout_ms >= UINT32_C(0x80000000)) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_cache_ttl_ms >= UINT32_C(0x80000000)) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_cache_policy != WL_RPC_CACHE_REJECT_NEW && config->rpc_server_cache_policy != WL_RPC_CACHE_EVICT_OLDEST) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_server_pending_slot_t)) alignment = _Alignof(wl_rpc_server_pending_slot_t);
    result = device_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_pending_slot_t), config->rpc_server_pending_slot_count, sizeof(wl_rpc_server_pending_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_pending_slots, NULL);
    if (result != WL_OK) return result;
    if (alignment < _Alignof(wl_rpc_server_cache_slot_t)) alignment = _Alignof(wl_rpc_server_cache_slot_t);
    result = device_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_cache_slot_t), config->rpc_server_cache_slot_count, sizeof(wl_rpc_server_cache_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_cache_slots, NULL);
    if (result != WL_OK) return result;
    result = device_runtime_storage_region(&cursor, 1U, config->rpc_server_cache_slot_count, config->rpc_server_response_capacity, out_layout == NULL ? NULL : &out_layout->rpc_server_responses, out_layout == NULL ? NULL : &out_layout->rpc_server_responses_size);
    if (result != WL_OK) return result;
  }
  if (out_requirements != NULL) out_requirements->rpc_result_count = config->rpc_client_enabled ? config->rpc_client_slot_count : 0U;
  if (out_requirements != NULL) {
    out_requirements->storage_size = cursor.offset;
    out_requirements->storage_alignment = alignment;
  }
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_requirements)
DEVICE_RPC_LAYOUT_device_runtime_requirements
#endif
int device_runtime_requirements(const device_runtime_config_t *config, device_runtime_requirements_t *out_requirements) {
  device_runtime_config_t config_copy;
  if (config == NULL || out_requirements == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  *out_requirements = (device_runtime_requirements_t){0};
  return device_runtime_layout(&config_copy, NULL, SIZE_MAX, NULL, out_requirements);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_init_validate)
DEVICE_RPC_LAYOUT_device_runtime_init_validate
#endif
static int device_runtime_init_validate(const device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage, device_runtime_requirements_t *requirements, device_runtime_init_diagnostic_t *diagnostic) {
  uintptr_t instance_address;
  uintptr_t storage_address;
  int result;
  if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
  if (instance == NULL || config == NULL || storage == NULL || requirements == NULL || diagnostic == NULL)
    return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_NULL_ARGUMENT, instance == NULL ? "instance" : config == NULL ? "config" : storage == NULL ? "storage" : requirements == NULL ? "requirements" : "diagnostic", 1U, 0U, WL_ERR_INVALID_ARG);
  if (device_runtime_roles_valid(config) != WL_OK) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_ROLE_ENABLE, "endpoint.rpc_role", 0U, 1U, WL_ERR_NOT_SUPPORTED);
  if (config->rpc_client_enabled > 1U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_ROLE_ENABLE, "rpc_client_enabled", 1U, config->rpc_client_enabled, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled > 1U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_ROLE_ENABLE, "rpc_server_enabled", 1U, config->rpc_server_enabled, WL_ERR_INVALID_ARG);
  if (config->rpc_client_enabled != 0U && config->rpc_client_slot_count == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_CLIENT_CAPACITY, "rpc_client_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_pending_slot_count == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_pending_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_cache_slot_count == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_cache_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_response_capacity == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_response_capacity", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_pending_timeout_ms >= UINT32_C(0x80000000)) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_TIMEOUT, "rpc_server_pending_timeout_ms", UINT32_C(0x7fffffff), config->rpc_server_pending_timeout_ms, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_cache_ttl_ms >= UINT32_C(0x80000000)) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_TIMEOUT, "rpc_server_cache_ttl_ms", UINT32_C(0x7fffffff), config->rpc_server_cache_ttl_ms, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_cache_policy != WL_RPC_CACHE_REJECT_NEW && config->rpc_server_cache_policy != WL_RPC_CACHE_EVICT_OLDEST) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_CACHE_POLICY, "rpc_server_cache_policy", 0U, (size_t)config->rpc_server_cache_policy, WL_ERR_INVALID_ARG);
  result = device_runtime_requirements(config, requirements);
  if (result != WL_OK) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_LAYOUT_OVERFLOW, "config", 0U, 0U, result);
  if (storage->size < requirements->storage_size) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_TOO_SMALL, "storage.size", requirements->storage_size, storage->size, WL_ERR_BUF_TOO_SMALL);
  if (requirements->storage_size != 0U) {
    if (storage->data == NULL) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_NULL, "storage.data", requirements->storage_size, 0U, WL_ERR_INVALID_ARG);
    if (((uintptr_t)storage->data & (requirements->storage_alignment - 1U)) != 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_ALIGNMENT, "storage.data", requirements->storage_alignment, (size_t)((uintptr_t)storage->data & (requirements->storage_alignment - 1U)), WL_ERR_INVALID_ARG);
    instance_address = (uintptr_t)(const void *)instance;
    storage_address = (uintptr_t)storage->data;
    if ((storage_address <= instance_address && instance_address - storage_address < requirements->storage_size) || (instance_address < storage_address && storage_address - instance_address < sizeof(*instance))) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_OVERLAP, "storage.data", requirements->storage_size, storage->size, WL_ERR_INVALID_ARG);
  }
  result = device_runtime_result_storage_validate(instance, config, storage, diagnostic);
  if (result != WL_OK) return result;
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_init)
DEVICE_RPC_LAYOUT_device_runtime_init
#endif
int device_runtime_init(device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage) {
  device_runtime_config_t config_copy;
  device_runtime_storage_t storage_copy;
  device_runtime_requirements_t requirements;
  device_runtime_layout_t layout;
  uintptr_t instance_address;
  uintptr_t storage_address;
  int result;
  if (instance == NULL || config == NULL || storage == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  storage_copy = *storage;
  config = &config_copy;
  storage = &storage_copy;
  result = device_runtime_requirements(config, &requirements);
  if (result != WL_OK) return result;
  if (storage->size < requirements.storage_size) return WL_ERR_BUF_TOO_SMALL;
  if (requirements.storage_size != 0U) {
    if (storage->data == NULL || ((uintptr_t)storage->data & (requirements.storage_alignment - 1U)) != 0U) return WL_ERR_INVALID_ARG;
    instance_address = (uintptr_t)(void *)instance;
    storage_address = (uintptr_t)storage->data;
    if ((storage_address <= instance_address && instance_address - storage_address < requirements.storage_size) || (instance_address < storage_address && storage_address - instance_address < sizeof(*instance))) return WL_ERR_INVALID_ARG;
  }
  result = device_runtime_result_storage_validate(instance, config, storage, NULL);
  if (result != WL_OK) return result;
  result = device_runtime_layout(config, (uint8_t *)storage->data, storage->size, &layout, NULL);
  if (result != WL_OK) return result;
  memset(instance, 0, sizeof(*instance));
#if DEVICE_RUNTIME_HAS_RPC_CLIENT
  if (config->rpc_client_enabled != 0U) {
    const wl_rpc_client_config_t client_config = {
      (wl_rpc_client_slot_t *)layout.rpc_client_slots,
      config->rpc_client_slot_count,
      config->rpc_client_next_operation_id
    };
    if (wl_rpc_client_init(&instance->rpc_client, &client_config) != WL_RPC_OK) {
      result = WL_ERR_INVALID_ARG;
      goto init_failed;
    }
    instance->runtime.rpc_client = &instance->rpc_client;
    instance->runtime.rpc_results = storage->rpc_results;
    instance->runtime.rpc_result_count = storage->rpc_result_count;
  }
#endif
#if DEVICE_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled) {
    const wl_rpc_server_config_t server_config = {
      .pending_slots = (wl_rpc_server_pending_slot_t *)layout.rpc_server_pending_slots,
      .pending_slot_count = config->rpc_server_pending_slot_count,
      .pending_timeout_ms = config->rpc_server_pending_timeout_ms,
      .cache_slots = (wl_rpc_server_cache_slot_t *)layout.rpc_server_cache_slots,
      .cache_slot_count = config->rpc_server_cache_slot_count,
      .response_storage = (uint8_t *)layout.rpc_server_responses,
      .response_storage_size = layout.rpc_server_responses_size,
      .response_capacity_per_slot = config->rpc_server_response_capacity,
      .cache_ttl_ms = config->rpc_server_cache_ttl_ms,
      .cache_policy = config->rpc_server_cache_policy,
    };
    if (wl_rpc_server_init(&instance->rpc_server, &server_config) != WL_RPC_OK) {
      result = WL_ERR_INVALID_ARG;
      goto init_failed;
    }
    instance->runtime.rpc_server = &instance->rpc_server;
  }
#endif
#if DEVICE_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled != 0U) {
    instance->runtime.configure.request_scratch = &instance->configure_scratch.request;
    instance->runtime.configure.request_handler = config->configure_request_handler;
    instance->runtime.configure.user_data = config->configure_user_data;
  }
#endif
#if DEVICE_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled != 0U) {
    instance->runtime.get_info.request_scratch = &instance->get_info_scratch.request;
    instance->runtime.get_info.request_handler = config->get_info_request_handler;
    instance->runtime.get_info.user_data = config->get_info_user_data;
  }
#endif
  return WL_OK;

init_failed:
  memset(instance, 0, sizeof(*instance));
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_init_checked)
DEVICE_RPC_LAYOUT_device_runtime_init_checked
#endif
int device_runtime_init_checked(device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage, device_runtime_init_diagnostic_t *out_diagnostic) {
  device_runtime_requirements_t requirements;
  int result = device_runtime_init_validate(instance, config, storage, &requirements, out_diagnostic);
  if (result != WL_OK) return result;
  result = device_runtime_init(instance, config, storage);
  if (result != WL_OK) return device_runtime_init_failure(out_diagnostic, DEVICE_RUNTIME_INIT_COMPONENT, "component", 0U, 0U, result);
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_rpc_response_writable)
DEVICE_RPC_LAYOUT_device_rpc_response_writable
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_rpc_response_writable)
#endif
static uint8_t device_rpc_response_writable(const wl_ctx_t *ctx, const device_runtime_t *runtime) {
  uint16_t message_id;
  if (runtime->rpc_response_retry_blocked &&
      runtime->rpc_response_retry_generation == wl_link_resource_generation(ctx)) return 0U;
  if (runtime->rpc_server == NULL || wl_rpc_server_response_peek(runtime->rpc_server, &message_id) != WL_RPC_OK) return 0U;
  switch (message_id) {
    case CONFIGURE_RESPONSE_MESSAGE_ID: return wl_tx_can_submit(ctx, WL_DELIVERY_RELIABLE);
    case INFO_RESPONSE_MESSAGE_ID: return wl_tx_can_submit(ctx, WL_DELIVERY_RELIABLE);
    default: return wl_tx_can_submit(ctx, WL_DELIVERY_UNRELIABLE);
  }
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_peer_observe)
DEVICE_RPC_LAYOUT_device_runtime_peer_observe
#endif
wl_rpc_err_t device_runtime_peer_observe(wl_ctx_t *ctx, device_runtime_t *runtime, uint64_t peer_session_id, wl_rpc_peer_observation_t *out_observation) {
  device_peer_cancel_context_t cancel = {ctx, runtime};
  wl_rpc_err_t result;
  if (out_observation != NULL) memset(out_observation, 0, sizeof(*out_observation));
  if (ctx == NULL || runtime == NULL || runtime->rpc_server == NULL || peer_session_id == 0U || out_observation == NULL) return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_peer_observe(runtime->rpc_server, &runtime->rpc_peer, peer_session_id, device_runtime_cancel_peer_tx, &cancel, out_observation);
  if (result == WL_RPC_OK && out_observation->changed != 0U) runtime->rpc_peer_observation = *out_observation;
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_peer_observation_take)
DEVICE_RPC_LAYOUT_device_runtime_peer_observation_take
#endif
wl_rpc_err_t device_runtime_peer_observation_take(device_runtime_t *runtime, wl_rpc_peer_observation_t *out_observation) {
  if (out_observation != NULL) memset(out_observation, 0, sizeof(*out_observation));
  if (runtime == NULL || out_observation == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_peer_observation.changed == 0U) return WL_RPC_ERR_NOT_FOUND;
  *out_observation = runtime->rpc_peer_observation;
  memset(&runtime->rpc_peer_observation, 0, sizeof(runtime->rpc_peer_observation));
  return WL_RPC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_poll)
DEVICE_RPC_LAYOUT_device_runtime_poll
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_runtime_poll)
#endif
wl_rpc_err_t device_runtime_poll(device_runtime_t *runtime, wl_time_ms_t now_ms, device_runtime_poll_result_t *out_result) {
  wl_rpc_err_t result;
  wl_rpc_server_expiry_t server_expiry = {0};
  if (out_result != NULL) memset(out_result, 0, sizeof(*out_result));
  if (runtime == NULL || out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_client != NULL) {
    result = wl_rpc_client_poll(runtime->rpc_client, now_ms, &out_result->client_timed_out);
    if (result != WL_RPC_OK) return result;
  }
  if (runtime->rpc_server != NULL) {
    result = wl_rpc_server_expired_acquire(runtime->rpc_server, now_ms, &out_result->server_expired_request);
    if (result == WL_RPC_OK) out_result->server_pending_expired = 1U;
    else if (result != WL_RPC_ERR_NOT_FOUND) return result;
    result = wl_rpc_server_poll(runtime->rpc_server, now_ms, &server_expiry);
    if (result != WL_RPC_OK) return result;
    out_result->server_cache_expired = server_expiry.cache_expired;
  }
  return WL_RPC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_service)
DEVICE_RPC_LAYOUT_device_runtime_service
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_runtime_service)
#endif
wl_rpc_err_t device_runtime_service(wl_ctx_t *ctx, device_runtime_t *runtime, wl_time_ms_t now_ms, device_runtime_service_result_t *out_result) {
  wl_rpc_server_response_t response = {0};
  wl_rpc_err_t result;
  uint8_t reliable_response = 0U;
  if (out_result != NULL) memset(out_result, 0, sizeof(*out_result));
  if (ctx == NULL || runtime == NULL || out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  out_result->response = device_runtime_result(NULL);
  if (runtime->rpc_retiring_tx != 0U) {
    wl_tx_result_t ignored;
    int retired = wl_tx_can_take(ctx, runtime->rpc_retiring_tx) ? wl_tx_take(ctx, runtime->rpc_retiring_tx, &ignored) : WL_ERR_BUSY;
    if (retired == WL_OK || retired == WL_ERR_NOT_FOUND) runtime->rpc_retiring_tx = 0U;
  }
  wl_rpc_deadline_hint_t timer;
  result = device_runtime_get_deadline_hint(runtime, now_ms, &timer);
  if (result != WL_RPC_OK) return result;
  if (timer.next_deadline_ms == 0U) {
    result = device_runtime_poll(runtime, now_ms, &out_result->deadlines);
    if (result != WL_RPC_OK) return result;
  }
  if (!device_rpc_response_writable(ctx, runtime)) return WL_RPC_OK;
  runtime->rpc_response_retry_blocked = 0U;
  result = wl_rpc_server_response_acquire(runtime->rpc_server, &response);
  if (result == WL_RPC_ERR_NOT_FOUND) return WL_RPC_OK;
  if (result != WL_RPC_OK) return result;
  out_result->response.message_id = response.identity.response_message_id;
  out_result->response.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  out_result->response.detail.rpc.operation_id = response.identity.operation_id;
  out_result->response.detail.rpc.application_result = response.application_status;
  out_result->response.detail.rpc.payload_length = response.response_length;
  out_result->response.detail.rpc.server_response = response;
  switch (response.identity.response_message_id) {
    case CONFIGURE_RESPONSE_MESSAGE_ID:
      if (response.identity.request_message_id != CONFIGURE_REQUEST_MESSAGE_ID) {
        result = WL_RPC_ERR_RESPONSE_MISMATCH;
        break;
      }
      reliable_response = 1U;
      break;
    case INFO_RESPONSE_MESSAGE_ID:
      if (response.identity.request_message_id != INFO_REQUEST_MESSAGE_ID) {
        result = WL_RPC_ERR_RESPONSE_MISMATCH;
        break;
      }
      reliable_response = 1U;
      break;
    default:
      result = WL_RPC_ERR_RESPONSE_MISMATCH;
      break;
  }
  if (result != WL_RPC_OK) {
    (void)wl_rpc_server_response_defer(runtime->rpc_server, &response);
    return result;
  }
  if (reliable_response != 0U) {
    out_result->response.detail.rpc.core_result = wl_send_reliable(ctx, response.identity.response_message_id, response.response_data, response.response_length, now_ms, &out_result->response.detail.rpc.handle);
  } else {
    out_result->response.detail.rpc.core_result = wl_send_unreliable(ctx, response.identity.response_message_id, response.response_data, response.response_length);
  }
  if (out_result->response.detail.rpc.core_result != WL_OK) {
    result = wl_rpc_server_response_defer(runtime->rpc_server, &response);
    if (result != WL_RPC_OK) return result;
    out_result->response.domain = DEVICE_RUNTIME_CORE_ERROR;
    out_result->responses_deferred = 1U;
    runtime->rpc_response_retry_generation = wl_link_resource_generation(ctx);
    runtime->rpc_response_retry_blocked = 1U;
    return WL_RPC_OK;
  }
  if (reliable_response != 0U) {
    result = wl_rpc_server_response_submitted(runtime->rpc_server, &response, out_result->response.detail.rpc.handle);
  } else {
    result = wl_rpc_server_response_sent(runtime->rpc_server, &response);
  }
  if (result != WL_RPC_OK) {
    (void)wl_rpc_server_response_defer(runtime->rpc_server, &response);
    return result;
  }
  out_result->response.domain = DEVICE_RUNTIME_OK;
  out_result->responses_submitted = 1U;
  return WL_RPC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_get_deadline_hint)
DEVICE_RPC_LAYOUT_device_runtime_get_deadline_hint
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_runtime_get_deadline_hint)
#endif
wl_rpc_err_t device_runtime_get_deadline_hint(const device_runtime_t *runtime, wl_time_ms_t now_ms, wl_rpc_deadline_hint_t *out_hint) {
  wl_rpc_deadline_hint_t component = {WL_RPC_NO_DEADLINE_MS};
  wl_rpc_err_t result;
  uint32_t nearest = WL_RPC_NO_DEADLINE_MS;
  if (out_hint != NULL) out_hint->next_deadline_ms = WL_RPC_NO_DEADLINE_MS;
  if (runtime == NULL || out_hint == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_client != NULL) {
    result = wl_rpc_client_get_deadline_hint(runtime->rpc_client, now_ms, &component);
    if (result != WL_RPC_OK) return result;
    if (component.next_deadline_ms < nearest) nearest = component.next_deadline_ms;
  }
  if (runtime->rpc_server != NULL) {
    result = wl_rpc_server_get_deadline_hint(runtime->rpc_server, now_ms, &component);
    if (result != WL_RPC_OK) return result;
    if (component.next_deadline_ms < nearest) nearest = component.next_deadline_ms;
  }
  out_hint->next_deadline_ms = nearest;
  return WL_RPC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_readiness)
DEVICE_RPC_LAYOUT_device_runtime_readiness
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_runtime_readiness)
#endif
wl_readiness_t device_runtime_readiness(const wl_ctx_t *ctx, const device_runtime_t *runtime, wl_time_ms_t now_ms) {
  wl_readiness_t value = wl_readiness_idle();
  wl_rpc_deadline_hint_t timer;
  uint16_t message_id = 0U;
  if (runtime == NULL || ctx == NULL) return value;
  if (device_runtime_get_deadline_hint(runtime, now_ms, &timer) == WL_RPC_OK)
    value = wl_readiness_at(timer.next_deadline_ms);
  if (runtime->rpc_retiring_tx != 0U) {
    if (wl_tx_can_take(ctx, runtime->rpc_retiring_tx)) value.runnable = 1U;
    else value.blocked = 1U;
  }
  if (runtime->rpc_server != NULL && wl_rpc_server_response_peek(runtime->rpc_server, &message_id) == WL_RPC_OK) {
    wl_delivery_t delivery = WL_DELIVERY_UNRELIABLE;
    switch (message_id) {
      case CONFIGURE_RESPONSE_MESSAGE_ID: delivery = WL_DELIVERY_RELIABLE; break;
      case INFO_RESPONSE_MESSAGE_ID: delivery = WL_DELIVERY_RELIABLE; break;
      default: break;
    }
    if (wl_tx_can_submit(ctx, delivery) && (!runtime->rpc_response_retry_blocked ||
        runtime->rpc_response_retry_generation != wl_link_resource_generation(ctx))) value.runnable = 1U;
    else value.blocked = 1U;
  }
  return value;
}

/* Keep RPC routing separate from the pump under LTO. */
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_dispatch_event)
DEVICE_RPC_LAYOUT_device_runtime_dispatch_event
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_runtime_dispatch_event)
#endif
device_runtime_result_t device_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, device_runtime_t *runtime, wl_time_ms_t now_ms) {
  device_runtime_result_t result = device_runtime_result(event);
  if (event == NULL) return result;
  (void)now_ms;
  if (event->type == WL_EVT_TX_SUCCESS || event->type == WL_EVT_TX_TIMEOUT || event->type == WL_EVT_TX_FAILED) {
    wl_tx_result_t tx_result = {0};
    if (runtime == NULL || ctx == NULL) {
      result.domain = DEVICE_RUNTIME_NON_RX;
      return result;
    }
    result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
    result.detail.rpc.handle = event->handle;
#if DEVICE_RUNTIME_HAS_MANAGED_RPC
    if (runtime->rpc_async != NULL && wl_rpc_async_retire_tx(runtime->rpc_async, event->handle)) {
      result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
      result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
      result.domain = result.detail.rpc.core_result == WL_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CORE_ERROR;
      return result;
    }
#endif
    if (runtime->rpc_server != NULL) {
      result.detail.rpc.rpc_result = wl_rpc_server_on_tx_event(runtime->rpc_server, event);
      if (result.detail.rpc.rpc_result == WL_RPC_OK) {
        result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
        result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
        result.domain = result.detail.rpc.core_result == WL_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CORE_ERROR;
        return result;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_ERR_NOT_FOUND) {
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        return result;
      }
    }
    if (runtime->rpc_client != NULL) {
      result.detail.rpc.rpc_result = wl_rpc_client_on_tx_event(runtime->rpc_client, event);
      if (result.detail.rpc.rpc_result == WL_RPC_OK) {
        result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
        result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
        result.domain = result.detail.rpc.core_result == WL_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CORE_ERROR;
      } else if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) result.domain = DEVICE_RUNTIME_NON_RX;
      else result.domain = DEVICE_RUNTIME_RPC_ERROR;
    } else {
      result.domain = DEVICE_RUNTIME_NON_RX;
    }
    return result;
  }
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX) {
    result.domain = DEVICE_RUNTIME_NON_RX;
    return result;
  }
  if (ctx == NULL) return result;
  if (runtime == NULL) goto release_event;

  switch (event->message_id) {
    case 5U: {
      wl_rpc_request_identity_t identity = {.request_fingerprint = device_rpc_fingerprint_seed};
      wl_rpc_server_request_t server_request;
      device_configure_request_token_t token;
      size_t canonical_length = 0U, header_length = 0U;
      uint64_t session = 0U;
      if (!device_rpc_request_prepare(event, runtime, WL_EVT_RELIABLE_RX, &result, &session, &header_length)) break;
      if (runtime->configure.request_scratch == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
        break;
      }
      result.detail.rpc.codec_status = device_rpc_configure_request_decode(event->payload + header_length,
          event->payload_len - header_length, runtime->configure.request_scratch);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.codec_status = device_rpc_configure_request_wlc_detail_fingerprint(runtime->configure.request_scratch,
          &identity.request_fingerprint, &canonical_length);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.payload_length = event->payload_len;
      identity.operation_id = result.detail.rpc.operation_id;
      identity.request_message_id = 5U;
      identity.response_message_id = 6U;
      identity.peer_session_id = session;
      if (!device_rpc_request_begin(ctx, runtime, &identity, canonical_length, now_ms, &result)) break;
      server_request = result.detail.rpc.server_request;
      memset(&token, 0, sizeof(token));
      token.private_state.owner = runtime;
      token.private_state.incarnation = runtime->rpc_incarnation;
      token.private_state.request = server_request;
#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
      if (runtime->configure.value_handler != NULL) {
        int32_t status;
        device_runtime_result_t completed;
        if (runtime->configure.request_value == NULL || runtime->configure.response_value == NULL) {
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
          result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
          break;
        }
        /* The view was decoded and has not crossed an application callback. */
        configure_request_wlc_detail_value_copy(
            runtime->configure.request_scratch, runtime->configure.request_value);
        configure_response_value_clear(runtime->configure.response_value);
        status = runtime->configure.value_handler(runtime->configure.value_user_data,
            runtime->configure.request_value, runtime->configure.response_value);
        completed = status == 0
            ? device_configure_server_complete_value(runtime, &token, runtime->configure.response_value, now_ms)
            : device_configure_server_reject(runtime, &token, status, now_ms);
        result.domain = completed.domain;
        result.detail.rpc = completed.detail.rpc;
        if (!device_runtime_result_ok(&completed))
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        break;
      }
#endif
      if (runtime->configure.request_handler == NULL) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.application_result = runtime->configure.request_handler(
          runtime->configure.user_data, runtime->configure.request_scratch, &token, WL_DELIVERY_RELIABLE);
      if (result.detail.rpc.application_result != 0) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_APPLICATION_ERROR;
      } else result.domain = DEVICE_RUNTIME_OK;
      break;
    }
    case 6U: {
      wl_rpc_client_response_t authority;
      uint16_t result_index;
      uint64_t session = 0U;
      size_t header_length = 0U;
      result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = DEVICE_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->rpc_client == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.rpc_result = device_rpc_header_read(event->payload,
          event->payload_len, 2U, &result.detail.rpc.operation_id,
          &result.detail.rpc.application_result, &session, &header_length);
      if (result.detail.rpc.rpc_result != WL_RPC_OK) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_MALFORMED_METADATA;
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (session != wl_link_session_id(ctx)) {
        /* Observable stale/misdirected reply; do not touch another call. */
        result.detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      result.detail.rpc.rpc_result = wl_rpc_client_response_prepare(runtime->rpc_client,
          5U, 6U, result.detail.rpc.operation_id, now_ms, &authority, &result_index);
      if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) {
        /* Unknown/late responses are observable, not failures of another call. */
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_OK) {
        result.domain = result.detail.rpc.rpc_result == WL_RPC_ERR_INVALID_STATE ?
            DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (runtime->rpc_results == NULL || result_index >= runtime->rpc_result_count) {
        result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
        break;
      }
      if (result.detail.rpc.application_result == 0) {
        result.detail.rpc.codec_status = device_rpc_configure_response_wlc_detail_value_decode(event->payload + header_length,
            event->payload_len - header_length, &runtime->rpc_results[result_index].configure);
        if (result.detail.rpc.codec_status != WL_CODEC_OK) {
          result.domain = DEVICE_RUNTIME_CODEC_ERROR;
          break;
        }
      }
      result.detail.rpc.payload_length = event->payload_len;
      result.detail.rpc.rpc_result = wl_rpc_client_response_commit(runtime->rpc_client,
          &authority, result.detail.rpc.application_result, now_ms);
      result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ?
          DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
      break;
    }
    case 2U: {
      wl_rpc_request_identity_t identity = {.request_fingerprint = device_rpc_fingerprint_seed};
      wl_rpc_server_request_t server_request;
      device_get_info_request_token_t token;
      size_t canonical_length = 0U, header_length = 0U;
      uint64_t session = 0U;
      if (!device_rpc_request_prepare(event, runtime, WL_EVT_RELIABLE_RX, &result, &session, &header_length)) break;
      if (runtime->get_info.request_scratch == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
        break;
      }
      result.detail.rpc.codec_status = device_rpc_info_request_decode(event->payload + header_length,
          event->payload_len - header_length, runtime->get_info.request_scratch);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.codec_status = device_rpc_info_request_wlc_detail_fingerprint(runtime->get_info.request_scratch,
          &identity.request_fingerprint, &canonical_length);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.payload_length = event->payload_len;
      identity.operation_id = result.detail.rpc.operation_id;
      identity.request_message_id = 2U;
      identity.response_message_id = 4U;
      identity.peer_session_id = session;
      if (!device_rpc_request_begin(ctx, runtime, &identity, canonical_length, now_ms, &result)) break;
      server_request = result.detail.rpc.server_request;
      memset(&token, 0, sizeof(token));
      token.private_state.owner = runtime;
      token.private_state.incarnation = runtime->rpc_incarnation;
      token.private_state.request = server_request;
#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
      if (runtime->get_info.value_handler != NULL) {
        int32_t status;
        device_runtime_result_t completed;
        if (runtime->get_info.request_value == NULL || runtime->get_info.response_value == NULL) {
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
          result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
          break;
        }
        /* The view was decoded and has not crossed an application callback. */
        info_request_wlc_detail_value_copy(
            runtime->get_info.request_scratch, runtime->get_info.request_value);
        info_response_value_clear(runtime->get_info.response_value);
        status = runtime->get_info.value_handler(runtime->get_info.value_user_data,
            runtime->get_info.request_value, runtime->get_info.response_value);
        completed = status == 0
            ? device_get_info_server_complete_value(runtime, &token, runtime->get_info.response_value, now_ms)
            : device_get_info_server_reject(runtime, &token, status, now_ms);
        result.domain = completed.domain;
        result.detail.rpc = completed.detail.rpc;
        if (!device_runtime_result_ok(&completed))
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        break;
      }
#endif
      if (runtime->get_info.request_handler == NULL) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.application_result = runtime->get_info.request_handler(
          runtime->get_info.user_data, runtime->get_info.request_scratch, &token, WL_DELIVERY_RELIABLE);
      if (result.detail.rpc.application_result != 0) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_APPLICATION_ERROR;
      } else result.domain = DEVICE_RUNTIME_OK;
      break;
    }
    case 4U: {
      wl_rpc_client_response_t authority;
      uint16_t result_index;
      uint64_t session = 0U;
      size_t header_length = 0U;
      result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = DEVICE_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->rpc_client == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.rpc_result = device_rpc_header_read(event->payload,
          event->payload_len, 2U, &result.detail.rpc.operation_id,
          &result.detail.rpc.application_result, &session, &header_length);
      if (result.detail.rpc.rpc_result != WL_RPC_OK) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_MALFORMED_METADATA;
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (session != wl_link_session_id(ctx)) {
        /* Observable stale/misdirected reply; do not touch another call. */
        result.detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      result.detail.rpc.rpc_result = wl_rpc_client_response_prepare(runtime->rpc_client,
          2U, 4U, result.detail.rpc.operation_id, now_ms, &authority, &result_index);
      if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) {
        /* Unknown/late responses are observable, not failures of another call. */
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_OK) {
        result.domain = result.detail.rpc.rpc_result == WL_RPC_ERR_INVALID_STATE ?
            DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (runtime->rpc_results == NULL || result_index >= runtime->rpc_result_count) {
        result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
        break;
      }
      if (result.detail.rpc.application_result == 0) {
        result.detail.rpc.codec_status = device_rpc_info_response_wlc_detail_value_decode(event->payload + header_length,
            event->payload_len - header_length, &runtime->rpc_results[result_index].get_info);
        if (result.detail.rpc.codec_status != WL_CODEC_OK) {
          result.domain = DEVICE_RUNTIME_CODEC_ERROR;
          break;
        }
      }
      result.detail.rpc.payload_length = event->payload_len;
      result.detail.rpc.rpc_result = wl_rpc_client_response_commit(runtime->rpc_client,
          &authority, result.detail.rpc.application_result, now_ms);
      result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ?
          DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
      break;
    }
    default:
      result.domain = DEVICE_RUNTIME_UNKNOWN_MESSAGE;
      break;
  }

release_event:
  wl_event_release(ctx, event);
  result.event_consumed = 1U;
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_client_start)
DEVICE_RPC_LAYOUT_device_configure_client_start
#endif
device_runtime_result_t device_configure_client_start(wl_ctx_t *ctx,
    device_runtime_t *runtime, const configure_request_t *request, uint32_t timeout_ms,
    wl_time_ms_t now_ms) {
  device_runtime_result_t result = device_runtime_result(NULL);
  wl_tx_payload_claim_t claim = {0};
  uint32_t operation_id = 0U;
  size_t encoded_length = 0U, header_length;
  result.message_id = 5U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL)
    return result;
  result.detail.rpc.rpc_result = wl_rpc_client_begin(runtime->rpc_client,
      5U, 6U, timeout_ms, now_ms, &operation_id);
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.operation_id = operation_id;
  result.detail.rpc.core_result = wl_tx_payload_claim(ctx, 5U, WL_DELIVERY_RELIABLE, &claim);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  header_length = device_rpc_header_size(operation_id, 0);
  if (claim.span.length < header_length) {
    result.detail.rpc.core_result = WL_ERR_BUF_TOO_SMALL;
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  /* Encode directly into the link claim: no whole-request copy or heap. */
  device_rpc_header_write(claim.span.data, 1U, operation_id, 0, wl_link_session_id(ctx));
  result.detail.rpc.codec_status = device_rpc_configure_request_encode(request, claim.span.data + header_length,
      claim.span.length - header_length, &encoded_length);
  if (result.detail.rpc.codec_status != WL_CODEC_OK) {
    result.detail.rpc.core_result = WL_ERR_CORRUPT_PAYLOAD;
    result.domain = DEVICE_RUNTIME_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  result.detail.rpc.payload_length = encoded_length + header_length;
  result.detail.rpc.core_result = wl_tx_payload_commit(ctx, &claim,
      encoded_length + header_length, now_ms, &result.detail.rpc.handle);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  result.detail.rpc.rpc_result = wl_rpc_client_bind_tx(runtime->rpc_client, operation_id, result.detail.rpc.handle);
  result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
  return result;
start_failed:
  result.detail.rpc.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client,
      operation_id, result.detail.rpc.core_result);
  if (result.detail.rpc.rpc_result == WL_RPC_OK)
    result.detail.rpc.rpc_result = wl_rpc_client_release(runtime->rpc_client, operation_id);
  if (result.detail.rpc.rpc_result == WL_RPC_OK) result.detail.rpc.operation_id = 0U;
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_client_inspect)
DEVICE_RPC_LAYOUT_device_configure_client_inspect
#endif
wl_rpc_err_t device_configure_client_inspect(const device_runtime_t *runtime,
    uint32_t operation_id, wl_rpc_client_result_t *out_client) {
  wl_rpc_err_t result;
  if (out_client != NULL) memset(out_client, 0, sizeof(*out_client));
  if (runtime == NULL || runtime->rpc_client == NULL || out_client == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_client_get(runtime->rpc_client, operation_id, out_client);
  if (result != WL_RPC_OK) return result;
  return out_client->request_message_id == 5U && out_client->response_message_id == 6U
      ? WL_RPC_OK : WL_RPC_ERR_RESPONSE_MISMATCH;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_client_release)
DEVICE_RPC_LAYOUT_device_configure_client_release
#endif
wl_rpc_err_t device_configure_client_release(device_runtime_t *runtime, uint32_t operation_id) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t result = device_configure_client_inspect(runtime, operation_id, &client);
  return result == WL_RPC_OK ? wl_rpc_client_release(runtime->rpc_client, operation_id) : result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_encode_response)
DEVICE_RPC_LAYOUT_device_configure_encode_response
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_configure_encode_response)
#endif
static wl_codec_status_t device_configure_encode_response(const void *response,
    uint8_t *out, size_t capacity, size_t *length, bool owned) {
#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
  if (owned) return device_rpc_configure_response_value_encode(response, out, capacity, length);
#else
  (void)owned;
#endif
  return device_rpc_configure_response_encode(response, out, capacity, length);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_server_finish)
DEVICE_RPC_LAYOUT_device_configure_server_finish
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_configure_server_finish)
#endif
static device_runtime_result_t device_configure_server_finish(device_runtime_t *runtime,
    const device_configure_request_token_t *token, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned) {
  device_runtime_result_t result = device_runtime_result(NULL);
  const wl_rpc_server_request_t *request;
  result.message_id = 6U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  result.detail.rpc.application_result = application_status;
  if (runtime == NULL || runtime->rpc_server == NULL || token == NULL ||
      token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      (application_status == 0 && response == NULL)) return result;
  request = &token->private_state.request;
  if (request->identity.request_message_id != 5U ||
      request->identity.response_message_id != 6U) return result;
  device_rpc_finish_response(runtime, request, application_status, response, now_ms,
      owned, device_configure_encode_response, &result);
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_request_inspect)
DEVICE_RPC_LAYOUT_device_configure_request_inspect
#endif
wl_rpc_err_t device_configure_request_inspect(device_runtime_t *runtime,
    const device_configure_request_token_t *token, wl_rpc_server_request_t *out_request) {
  if (runtime == NULL || token == NULL || out_request == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  if (token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      token->private_state.request.identity.request_message_id != 5U ||
      token->private_state.request.identity.response_message_id != 6U)
    return WL_RPC_ERR_NOT_FOUND;
  *out_request = token->private_state.request;
  return WL_RPC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_server_complete)
DEVICE_RPC_LAYOUT_device_configure_server_complete
#endif
device_runtime_result_t device_configure_server_complete(device_runtime_t *runtime,
    const device_configure_request_token_t *token, const configure_response_t *response, wl_time_ms_t now_ms) {
  return device_configure_server_finish(runtime, token, 0, response, now_ms, false);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_server_reject)
DEVICE_RPC_LAYOUT_device_configure_server_reject
#endif
device_runtime_result_t device_configure_server_reject(device_runtime_t *runtime,
    const device_configure_request_token_t *token, int32_t application_status, wl_time_ms_t now_ms) {
  return device_configure_server_finish(runtime, token, application_status, NULL, now_ms, false);
}

#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_server_complete_value)
DEVICE_RPC_LAYOUT_device_configure_server_complete_value
#endif
device_runtime_result_t device_configure_server_complete_value(device_runtime_t *runtime,
    const device_configure_request_token_t *token, const configure_response_value_t *response,
    wl_time_ms_t now_ms) {
  return device_configure_server_finish(runtime, token, 0, response, now_ms, true);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_encode_submission)
DEVICE_RPC_LAYOUT_device_configure_encode_submission
#endif
wl_err_t device_configure_encode_submission(const void *request, uint32_t operation_id, uint64_t session,
    uint8_t *out, size_t capacity, size_t *length) {
  wl_codec_status_t status;
  size_t size = 0U;
  const size_t header_length = device_rpc_header_size(operation_id, 0);
  if (capacity < header_length) return WL_ERR_BUF_TOO_SMALL;
  if (session == 0U) return WL_ERR_INVALID_ARG;
  device_rpc_header_write(out, 1U, operation_id, 0, session);
  status = device_rpc_configure_request_value_encode(request, out + header_length, capacity - header_length, &size);
  if (status != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  *length = size + header_length;
  return WL_OK;
}
#endif

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_configure_client_response)
DEVICE_RPC_LAYOUT_device_configure_client_response
#endif
wl_rpc_err_t device_configure_client_response(const device_runtime_t *runtime, uint32_t operation_id, const configure_response_value_t **out_response) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error;
  if (out_response == NULL) return WL_RPC_ERR_INVALID_ARG;
  *out_response = NULL;
  error = device_configure_client_inspect(runtime, operation_id, &client);
  if (error != WL_RPC_OK) return error;
  if (!client.response_valid || runtime->rpc_results == NULL || client.result_index >= runtime->rpc_result_count) return WL_RPC_ERR_INVALID_STATE;
  *out_response = &runtime->rpc_results[client.result_index].configure;
  return WL_RPC_OK;
}


#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_client_start)
DEVICE_RPC_LAYOUT_device_get_info_client_start
#endif
device_runtime_result_t device_get_info_client_start(wl_ctx_t *ctx,
    device_runtime_t *runtime, const info_request_t *request, uint32_t timeout_ms,
    wl_time_ms_t now_ms) {
  device_runtime_result_t result = device_runtime_result(NULL);
  wl_tx_payload_claim_t claim = {0};
  uint32_t operation_id = 0U;
  size_t encoded_length = 0U, header_length;
  result.message_id = 2U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL)
    return result;
  result.detail.rpc.rpc_result = wl_rpc_client_begin(runtime->rpc_client,
      2U, 4U, timeout_ms, now_ms, &operation_id);
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.operation_id = operation_id;
  result.detail.rpc.core_result = wl_tx_payload_claim(ctx, 2U, WL_DELIVERY_RELIABLE, &claim);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  header_length = device_rpc_header_size(operation_id, 0);
  if (claim.span.length < header_length) {
    result.detail.rpc.core_result = WL_ERR_BUF_TOO_SMALL;
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  /* Encode directly into the link claim: no whole-request copy or heap. */
  device_rpc_header_write(claim.span.data, 1U, operation_id, 0, wl_link_session_id(ctx));
  result.detail.rpc.codec_status = device_rpc_info_request_encode(request, claim.span.data + header_length,
      claim.span.length - header_length, &encoded_length);
  if (result.detail.rpc.codec_status != WL_CODEC_OK) {
    result.detail.rpc.core_result = WL_ERR_CORRUPT_PAYLOAD;
    result.domain = DEVICE_RUNTIME_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  result.detail.rpc.payload_length = encoded_length + header_length;
  result.detail.rpc.core_result = wl_tx_payload_commit(ctx, &claim,
      encoded_length + header_length, now_ms, &result.detail.rpc.handle);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  result.detail.rpc.rpc_result = wl_rpc_client_bind_tx(runtime->rpc_client, operation_id, result.detail.rpc.handle);
  result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
  return result;
start_failed:
  result.detail.rpc.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client,
      operation_id, result.detail.rpc.core_result);
  if (result.detail.rpc.rpc_result == WL_RPC_OK)
    result.detail.rpc.rpc_result = wl_rpc_client_release(runtime->rpc_client, operation_id);
  if (result.detail.rpc.rpc_result == WL_RPC_OK) result.detail.rpc.operation_id = 0U;
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_client_inspect)
DEVICE_RPC_LAYOUT_device_get_info_client_inspect
#endif
wl_rpc_err_t device_get_info_client_inspect(const device_runtime_t *runtime,
    uint32_t operation_id, wl_rpc_client_result_t *out_client) {
  wl_rpc_err_t result;
  if (out_client != NULL) memset(out_client, 0, sizeof(*out_client));
  if (runtime == NULL || runtime->rpc_client == NULL || out_client == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_client_get(runtime->rpc_client, operation_id, out_client);
  if (result != WL_RPC_OK) return result;
  return out_client->request_message_id == 2U && out_client->response_message_id == 4U
      ? WL_RPC_OK : WL_RPC_ERR_RESPONSE_MISMATCH;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_client_release)
DEVICE_RPC_LAYOUT_device_get_info_client_release
#endif
wl_rpc_err_t device_get_info_client_release(device_runtime_t *runtime, uint32_t operation_id) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t result = device_get_info_client_inspect(runtime, operation_id, &client);
  return result == WL_RPC_OK ? wl_rpc_client_release(runtime->rpc_client, operation_id) : result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_encode_response)
DEVICE_RPC_LAYOUT_device_get_info_encode_response
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_get_info_encode_response)
#endif
static wl_codec_status_t device_get_info_encode_response(const void *response,
    uint8_t *out, size_t capacity, size_t *length, bool owned) {
#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
  if (owned) return device_rpc_info_response_value_encode(response, out, capacity, length);
#else
  (void)owned;
#endif
  return device_rpc_info_response_encode(response, out, capacity, length);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_server_finish)
DEVICE_RPC_LAYOUT_device_get_info_server_finish
#elif WLC_DEVICE_RPC_LAYOUT_ACTIVE
DEVICE_RPC_LAYOUT(device_get_info_server_finish)
#endif
static device_runtime_result_t device_get_info_server_finish(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned) {
  device_runtime_result_t result = device_runtime_result(NULL);
  const wl_rpc_server_request_t *request;
  result.message_id = 4U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  result.detail.rpc.application_result = application_status;
  if (runtime == NULL || runtime->rpc_server == NULL || token == NULL ||
      token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      (application_status == 0 && response == NULL)) return result;
  request = &token->private_state.request;
  if (request->identity.request_message_id != 2U ||
      request->identity.response_message_id != 4U) return result;
  device_rpc_finish_response(runtime, request, application_status, response, now_ms,
      owned, device_get_info_encode_response, &result);
  return result;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_request_inspect)
DEVICE_RPC_LAYOUT_device_get_info_request_inspect
#endif
wl_rpc_err_t device_get_info_request_inspect(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, wl_rpc_server_request_t *out_request) {
  if (runtime == NULL || token == NULL || out_request == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  if (token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      token->private_state.request.identity.request_message_id != 2U ||
      token->private_state.request.identity.response_message_id != 4U)
    return WL_RPC_ERR_NOT_FOUND;
  *out_request = token->private_state.request;
  return WL_RPC_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_server_complete)
DEVICE_RPC_LAYOUT_device_get_info_server_complete
#endif
device_runtime_result_t device_get_info_server_complete(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, const info_response_t *response, wl_time_ms_t now_ms) {
  return device_get_info_server_finish(runtime, token, 0, response, now_ms, false);
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_server_reject)
DEVICE_RPC_LAYOUT_device_get_info_server_reject
#endif
device_runtime_result_t device_get_info_server_reject(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, int32_t application_status, wl_time_ms_t now_ms) {
  return device_get_info_server_finish(runtime, token, application_status, NULL, now_ms, false);
}

#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_server_complete_value)
DEVICE_RPC_LAYOUT_device_get_info_server_complete_value
#endif
device_runtime_result_t device_get_info_server_complete_value(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, const info_response_value_t *response,
    wl_time_ms_t now_ms) {
  return device_get_info_server_finish(runtime, token, 0, response, now_ms, true);
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_encode_submission)
DEVICE_RPC_LAYOUT_device_get_info_encode_submission
#endif
wl_err_t device_get_info_encode_submission(const void *request, uint32_t operation_id, uint64_t session,
    uint8_t *out, size_t capacity, size_t *length) {
  wl_codec_status_t status;
  size_t size = 0U;
  const size_t header_length = device_rpc_header_size(operation_id, 0);
  if (capacity < header_length) return WL_ERR_BUF_TOO_SMALL;
  if (session == 0U) return WL_ERR_INVALID_ARG;
  device_rpc_header_write(out, 1U, operation_id, 0, session);
  status = device_rpc_info_request_value_encode(request, out + header_length, capacity - header_length, &size);
  if (status != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  *length = size + header_length;
  return WL_OK;
}
#endif

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_get_info_client_response)
DEVICE_RPC_LAYOUT_device_get_info_client_response
#endif
wl_rpc_err_t device_get_info_client_response(const device_runtime_t *runtime, uint32_t operation_id, const info_response_value_t **out_response) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error;
  if (out_response == NULL) return WL_RPC_ERR_INVALID_ARG;
  *out_response = NULL;
  error = device_get_info_client_inspect(runtime, operation_id, &client);
  if (error != WL_RPC_OK) return error;
  if (!client.response_valid || runtime->rpc_results == NULL || client.result_index >= runtime->rpc_result_count) return WL_RPC_ERR_INVALID_STATE;
  *out_response = &runtime->rpc_results[client.result_index].get_info;
  return WL_RPC_OK;
}


#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_pump_event)
DEVICE_RPC_LAYOUT_device_runtime_pump_event
#endif
static wl_pump_event_disposition_t device_runtime_pump_event(void *user_data, wl_ctx_t *ctx, const wl_event_t *event, wl_time_ms_t now_ms) {
  device_runtime_pump_t *pump = (device_runtime_pump_t *)user_data;
  device_runtime_result_t result;
  if (pump == NULL || pump->runtime == NULL) return WL_PUMP_EVENT_UNHANDLED;
  result = device_runtime_dispatch_event(ctx, event, pump->runtime, now_ms);
  if (pump->on_result != NULL) pump->on_result(pump->user_data, &result);
  return result.event_consumed != 0U ? WL_PUMP_EVENT_CONSUMED : WL_PUMP_EVENT_UNHANDLED;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_pump_step)
DEVICE_RPC_LAYOUT_device_runtime_pump_step
#endif
static uint8_t device_runtime_pump_step(void *user_data, wl_ctx_t *ctx,
    wl_time_ms_t now_ms) {
  device_runtime_pump_t *pump = (device_runtime_pump_t *)user_data;
  uint8_t did_work = 0U;
  if (pump == NULL || pump->runtime == NULL) return 0U;
  /* The pump checked combined readiness before calling application_step.
   * Service rechecks its deadlines and transport before taking any work. */
  {
    pump->last_service_result = device_runtime_service(ctx, pump->runtime, now_ms, &pump->last_service);
    if (pump->last_service_result == WL_RPC_OK) {
      if (pump->last_service.response.message_id != 0U && pump->on_result != NULL)
        pump->on_result(pump->user_data, &pump->last_service.response);
      did_work = pump->last_service.responses_submitted != 0U ||
          pump->last_service.responses_deferred != 0U ||
          pump->last_service.deadlines.client_timed_out != 0U ||
          pump->last_service.deadlines.server_pending_expired != 0U ||
          pump->last_service.deadlines.server_cache_expired != 0U;
    }
  }
  if (pump->runtime->rpc_async != NULL &&
      wl_rpc_async_get_readiness(pump->runtime->rpc_async).runnable) {
    wl_rpc_async_service_result_t work = {0};
    if (wl_rpc_async_service(pump->runtime->rpc_async, now_ms, &work) != WL_OK)
      pump->last_service_result = WL_RPC_ERR_INVALID_STATE;
    did_work |= work.did_work;
  }
  return did_work;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_pump_readiness)
DEVICE_RPC_LAYOUT_device_runtime_pump_readiness
#endif
static wl_readiness_t device_runtime_pump_readiness(const void *user_data,
    const wl_ctx_t *ctx, wl_time_ms_t now_ms) {
  const device_runtime_pump_t *pump = (const device_runtime_pump_t *)user_data;
  if (pump == NULL || pump->runtime == NULL) return wl_readiness_idle();
  wl_readiness_t value = device_runtime_readiness(ctx, pump->runtime, now_ms);
  if (pump->runtime->rpc_async != NULL)
    wl_readiness_merge(&value, wl_rpc_async_get_readiness(pump->runtime->rpc_async));
  return value;
}
#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_pump_init)
DEVICE_RPC_LAYOUT_device_runtime_pump_init
#endif
wl_err_t device_runtime_pump_init(device_runtime_pump_t *pump, device_runtime_t *runtime, device_runtime_result_fn on_result, void *user_data) {
  if (pump == NULL || runtime == NULL) return WL_ERR_INVALID_ARG;
  memset(pump, 0, sizeof(*pump));
  pump->runtime = runtime;
  pump->user_data = user_data;
  pump->on_result = on_result;
  return WL_OK;
}

#if WLC_DEVICE_RPC_LAYOUT_ACTIVE && defined(DEVICE_RPC_LAYOUT_device_runtime_pump_hooks)
DEVICE_RPC_LAYOUT_device_runtime_pump_hooks
#endif
wl_pump_hooks_t device_runtime_pump_hooks(device_runtime_pump_t *pump) {
  wl_pump_hooks_t hooks = {0};
  if (pump == NULL) return hooks;
  hooks.application_user_data = pump;
  hooks.on_event = device_runtime_pump_event;
  hooks.application_step = device_runtime_pump_step;
  hooks.application_readiness = device_runtime_pump_readiness;
  return hooks;
}

#undef WLC_DEVICE_RPC_LAYOUT_ACTIVE
#ifdef WLC_DEVICE_RPC_LAYOUT_DEFAULT
#undef DEVICE_RPC_LAYOUT
#undef WLC_DEVICE_RPC_LAYOUT_DEFAULT
#endif
#ifdef WLC_DEVICE_RPC_LAYOUT_MODULE_DEFAULT
#undef DEVICE_RPC_LAYOUT_ENABLE
#undef WLC_DEVICE_RPC_LAYOUT_MODULE_DEFAULT
#endif
#ifdef WLC_DEVICE_RPC_LAYOUT_GLOBAL_DEFAULT
#undef WLC_RPC_LAYOUT_ENABLE
#undef WLC_DEVICE_RPC_LAYOUT_GLOBAL_DEFAULT
#endif
