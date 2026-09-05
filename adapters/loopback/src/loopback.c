/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/loopback.h"

#include <string.h>

#define WL_LOOPBACK_MAGIC UINT32_C(0x574c4c42)
#define WL_LOOPBACK_ENDPOINT_COUNT 2U

typedef struct wl_loopback_impl wl_loopback_impl_t;

typedef struct wl_loopback_direction {
  wl_loopback_impl_t *owner;
  const uint8_t *data;
  size_t length;
  wl_io_token_t token;
  uint8_t source;
  uint8_t pending;
} wl_loopback_direction_t;

struct wl_loopback_impl {
  uint32_t magic;
  wl_ctx_t *links[WL_LOOPBACK_ENDPOINT_COUNT];
  wl_loopback_direction_t directions[WL_LOOPBACK_ENDPOINT_COUNT];
  wl_adapter_stats_t stats[WL_LOOPBACK_ENDPOINT_COUNT];
  uint8_t started;
  uint8_t next_direction;
};

_Static_assert(sizeof(wl_loopback_impl_t) <= WL_LOOPBACK_STORAGE_SIZE,
               "WL_LOOPBACK_STORAGE_SIZE is too small");
_Static_assert(_Alignof(wl_loopback_t) >= _Alignof(wl_loopback_impl_t),
               "wl_loopback_t alignment is too small");

static wl_loopback_impl_t *loopback_impl(wl_loopback_t *loopback) {
  return (wl_loopback_impl_t *)(void *)loopback;
}

static const wl_loopback_impl_t *loopback_impl_const(
    const wl_loopback_t *loopback) {
  return (const wl_loopback_impl_t *)(const void *)loopback;
}

static int endpoint_valid(wl_loopback_endpoint_t endpoint) {
  return endpoint == WL_LOOPBACK_ENDPOINT_A ||
         endpoint == WL_LOOPBACK_ENDPOINT_B;
}

static int loopback_endpoint_service(void *context) {
  wl_loopback_service_result_t result;
  return wl_loopback_service(context, 2U, &result);
}

static void loopback_endpoint_close(void *context) {
  wl_loopback_quiesce(context);
}

static uint32_t loopback_endpoint_hint(const void *context, wl_time_ms_t now_ms) {
  const wl_loopback_impl_t *impl = loopback_impl_const(context);
  (void)now_ms;
  return impl->started && (impl->directions[0].pending || impl->directions[1].pending)
             ? 0U : WL_POLL_NO_DEADLINE_MS;
}

wl_err_t wl_loopback_connect(wl_loopback_t *loopback, wl_endpoint_t *endpoint_a,
                            wl_endpoint_t *endpoint_b) {
  wl_pump_hooks_t hooks = {0};
  int result;
  if (loopback == NULL || endpoint_a == endpoint_b) return WL_ERR_INVALID_ARG;
  if (wl_endpoint_link(endpoint_a) == NULL || wl_endpoint_link(endpoint_b) == NULL)
    return WL_ERR_NOT_INITIALIZED;
  if (wl_endpoint_has_adapter(endpoint_a) || wl_endpoint_has_adapter(endpoint_b))
    return WL_ERR_BUSY;
  result = wl_loopback_init(loopback, wl_endpoint_link(endpoint_a),
                            wl_endpoint_link(endpoint_b));
  if (result != WL_OK) return result;
  hooks.adapter_user_data = loopback;
  hooks.service = loopback_endpoint_service;
  hooks.quiesce = loopback_endpoint_close;
  hooks.adapter_deadline_hint = loopback_endpoint_hint;
  /* Preflight above makes attachment infallible on this single owner. */
  (void)wl_endpoint_attach(endpoint_a, &hooks);
  (void)wl_endpoint_attach(endpoint_b, &hooks);
  return WL_OK;
}

static wl_sink_result_t loopback_sink(void *user_data, wl_io_token_t token,
                                      const uint8_t *data, size_t length) {
  wl_loopback_direction_t *direction = user_data;
  wl_loopback_impl_t *impl;
  wl_adapter_stats_t *stats;

  if (direction == NULL || direction->owner == NULL) {
    return WL_SINK_FAILED;
  }
  impl = direction->owner;
  stats = &impl->stats[direction->source];
  if (impl->magic != WL_LOOPBACK_MAGIC || impl->started == 0U) {
    stats->errors++;
    return WL_SINK_FAILED;
  }
  if ((data == NULL && length != 0U) || direction->pending != 0U) {
    if (direction->pending != 0U) {
      return WL_SINK_BUSY;
    }
    stats->errors++;
    return WL_SINK_FAILED;
  }

  direction->data = data;
  direction->length = length;
  direction->token = token;
  direction->pending = 1U;
  stats->tx_units++;
  stats->tx_bytes += length;
  stats->tx_active = 1U;
  return WL_SINK_STARTED;
}

wl_err_t wl_loopback_init(wl_loopback_t *loopback, wl_ctx_t *endpoint_a,
                          wl_ctx_t *endpoint_b) {
  wl_loopback_impl_t *impl;
  wl_config_t config_a;
  wl_config_t config_b;
  int result;
  size_t index;

  if (loopback == NULL || endpoint_a == NULL || endpoint_b == NULL ||
      endpoint_a == endpoint_b) {
    return WL_ERR_INVALID_ARG;
  }
  result = wl_get_config(endpoint_a, &config_a);
  if (result != WL_OK) {
    return result;
  }
  result = wl_get_config(endpoint_b, &config_b);
  if (result != WL_OK) {
    return result;
  }
  if (config_a.envelope != WL_ENVELOPE_NATIVE_PACKET ||
      config_b.envelope != WL_ENVELOPE_NATIVE_PACKET) {
    return WL_ERR_NOT_SUPPORTED;
  }
  if (config_a.integrity != config_b.integrity) {
    return WL_ERR_INVALID_ARG;
  }

  memset(loopback, 0, sizeof(*loopback));
  impl = loopback_impl(loopback);
  impl->magic = WL_LOOPBACK_MAGIC;
  impl->links[WL_LOOPBACK_ENDPOINT_A] = endpoint_a;
  impl->links[WL_LOOPBACK_ENDPOINT_B] = endpoint_b;
  for (index = 0U; index < WL_LOOPBACK_ENDPOINT_COUNT; ++index) {
    impl->directions[index].owner = impl;
    impl->directions[index].source = (uint8_t)index;
  }

  result = wl_set_sink(endpoint_a, loopback_sink,
                       &impl->directions[WL_LOOPBACK_ENDPOINT_A]);
  if (result != WL_OK) {
    memset(loopback, 0, sizeof(*loopback));
    return result;
  }
  result = wl_set_sink(endpoint_b, loopback_sink,
                       &impl->directions[WL_LOOPBACK_ENDPOINT_B]);
  if (result != WL_OK) {
    (void)wl_set_sink(endpoint_a, NULL, NULL);
    memset(loopback, 0, sizeof(*loopback));
    return result;
  }
  impl->started = 1U;
  impl->stats[WL_LOOPBACK_ENDPOINT_A].started = 1U;
  impl->stats[WL_LOOPBACK_ENDPOINT_B].started = 1U;
  return WL_OK;
}

static int next_pending_direction(const wl_loopback_impl_t *impl,
                                  uint8_t blocked_mask) {
  uint8_t offset;

  for (offset = 0U; offset < WL_LOOPBACK_ENDPOINT_COUNT; ++offset) {
    const uint8_t index =
        (uint8_t)((impl->next_direction + offset) %
                  WL_LOOPBACK_ENDPOINT_COUNT);
    if (impl->directions[index].pending != 0U &&
        (blocked_mask & (uint8_t)(1U << index)) == 0U) {
      return (int)index;
    }
  }
  return -1;
}

wl_err_t wl_loopback_service(wl_loopback_t *loopback, size_t unit_budget,
                             wl_loopback_service_result_t *out_result) {
  wl_loopback_service_result_t service = {0};
  wl_loopback_impl_t *impl;
  uint8_t blocked_mask = 0U;
  int first_error = WL_OK;

  if (loopback == NULL || unit_budget == 0U || out_result == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  impl = loopback_impl(loopback);
  if (impl->magic != WL_LOOPBACK_MAGIC || impl->started == 0U) {
    return WL_ERR_INVALID_STATE;
  }
  impl->stats[WL_LOOPBACK_ENDPOINT_A].service_calls++;
  impl->stats[WL_LOOPBACK_ENDPOINT_B].service_calls++;

  while (service.attempts < unit_budget) {
    wl_loopback_direction_t *direction;
    wl_adapter_stats_t *source_stats;
    wl_adapter_stats_t *destination_stats;
    wl_ctx_t *source;
    wl_ctx_t *destination;
    uint8_t source_index;
    uint8_t destination_index;
    wl_io_token_t token;
    size_t length;
    int delivery;
    int completion;
    int selected = next_pending_direction(impl, blocked_mask);

    if (selected < 0) {
      break;
    }
    source_index = (uint8_t)selected;
    destination_index = (uint8_t)(1U - source_index);
    direction = &impl->directions[source_index];
    source = impl->links[source_index];
    destination = impl->links[destination_index];
    source_stats = &impl->stats[source_index];
    destination_stats = &impl->stats[destination_index];
    impl->next_direction = destination_index;
    service.attempts++;

    delivery = wl_feed_unit(destination, direction->data, direction->length);
    if (delivery == WL_ERR_WOULD_BLOCK) {
      blocked_mask |= (uint8_t)(1U << source_index);
      destination_stats->rx_backpressure++;
      destination_stats->rx_paused = 1U;
      service.blocked++;
      continue;
    }

    token = direction->token;
    length = direction->length;
    direction->data = NULL;
    direction->length = 0U;
    direction->token = 0U;
    direction->pending = 0U;
    source_stats->tx_active = 0U;
    destination_stats->rx_paused = 0U;
    destination_stats->rx_units++;
    destination_stats->rx_bytes += length;
    if (delivery != WL_OK) {
      destination_stats->errors++;
      service.errors++;
      if (first_error == WL_OK) {
        first_error = delivery;
      }
    }

    completion = wl_tx_complete(source, token, WL_OK);
    source_stats->tx_completions++;
    if (completion != WL_OK) {
      source_stats->errors++;
      service.errors++;
      if (first_error == WL_OK) {
        first_error = completion;
      }
    }
    service.delivered++;
  }

  *out_result = service;
  if (first_error != WL_OK) {
    return first_error;
  }
  if (service.delivered != 0U) {
    return WL_OK;
  }
  return service.blocked != 0U ? WL_ERR_WOULD_BLOCK : WL_ERR_NO_DATA;
}

void wl_loopback_quiesce(wl_loopback_t *loopback) {
  wl_loopback_impl_t *impl;
  size_t index;

  if (loopback == NULL) {
    return;
  }
  impl = loopback_impl(loopback);
  if (impl->magic != WL_LOOPBACK_MAGIC || impl->started == 0U) {
    return;
  }
  impl->started = 0U;
  for (index = 0U; index < WL_LOOPBACK_ENDPOINT_COUNT; ++index) {
    impl->stats[index].started = 0U;
    (void)wl_set_sink(impl->links[index], NULL, NULL);
  }
  for (index = 0U; index < WL_LOOPBACK_ENDPOINT_COUNT; ++index) {
    wl_loopback_direction_t *direction = &impl->directions[index];
    if (direction->pending != 0U) {
      const wl_io_token_t token = direction->token;
      direction->data = NULL;
      direction->length = 0U;
      direction->token = 0U;
      direction->pending = 0U;
      impl->stats[index].tx_active = 0U;
      impl->stats[index].tx_completions++;
      impl->stats[index].errors++;
      (void)wl_tx_complete(impl->links[index], token, WL_ERR_IO);
    }
  }
}

wl_err_t wl_loopback_get_stats(const wl_loopback_t *loopback,
                               wl_loopback_endpoint_t endpoint,
                               wl_adapter_stats_t *out_stats) {
  const wl_loopback_impl_t *impl;

  if (loopback == NULL || out_stats == NULL || !endpoint_valid(endpoint)) {
    return WL_ERR_INVALID_ARG;
  }
  impl = loopback_impl_const(loopback);
  if (impl->magic != WL_LOOPBACK_MAGIC) {
    return WL_ERR_NOT_INITIALIZED;
  }
  *out_stats = impl->stats[(size_t)endpoint];
  return WL_OK;
}

wl_err_t wl_loopback_reset_stats(wl_loopback_t *loopback) {
  wl_loopback_impl_t *impl;
  size_t index;

  if (loopback == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  impl = loopback_impl(loopback);
  if (impl->magic != WL_LOOPBACK_MAGIC) {
    return WL_ERR_NOT_INITIALIZED;
  }
  for (index = 0U; index < WL_LOOPBACK_ENDPOINT_COUNT; ++index) {
    const uint8_t tx_active = impl->stats[index].tx_active;
    const uint8_t rx_paused = impl->stats[index].rx_paused;
    memset(&impl->stats[index], 0, sizeof(impl->stats[index]));
    impl->stats[index].started = impl->started;
    impl->stats[index].tx_active = tx_active;
    impl->stats[index].rx_paused = rx_paused;
  }
  return WL_OK;
}
