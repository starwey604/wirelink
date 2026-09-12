/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/udp.h"
#include "socket_contract.h"

#include <limits.h>
#include <string.h>

#ifdef CONFIG_WIRELINK_ZEPHYR_UDP_TIMING
static void timing_finish(wl_zephyr_udp_timing_t *timing, uint32_t started) {
  const uint32_t elapsed = k_cycle_get_32() - started;
  ++timing->calls;
  timing->cycles += elapsed;
  if (elapsed > timing->max_cycles) timing->max_cycles = elapsed;
}
#define TIMING_START() const uint32_t timing_started = k_cycle_get_32()
#define TIMING_FINISH(adapter, member) \
  timing_finish(&(adapter)->private_state.stats.member, timing_started)
#else
#define TIMING_START() ((void)0)
#define TIMING_FINISH(adapter, member) ((void)0)
#endif

static wl_err_t active(const wl_zephyr_udp_t *adapter) {
  if (adapter == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  if (!adapter->private_state.io.opened) return WL_ERR_NOT_INITIALIZED;
  if (atomic_get(&adapter->private_state.io.stopped)) return WL_ERR_CANCELLED;
  return adapter->private_state.stats.last_error;
}

static wl_err_t failed(wl_zephyr_udp_t *adapter, wl_err_t error) {
  if (adapter->private_state.stats.last_error == WL_OK) {
    ++adapter->private_state.stats.common.errors;
    adapter->private_state.stats.last_error = error;
  }
  adapter->private_state.retry_pending = false;
  return error;
}

static uint32_t retry_remaining(const wl_zephyr_udp_t *adapter, wl_time_ms_t now) {
  const int32_t remaining = (int32_t)(adapter->private_state.retry_due - now);
  return remaining > 0 ? (uint32_t)remaining : 0U;
}

static uint32_t deadline(const void *context, wl_time_ms_t now) {
  const wl_zephyr_udp_t *adapter = context;
  if (atomic_get(&adapter->private_state.io.stopped) ||
      adapter->private_state.stats.last_error != WL_OK || adapter->private_state.more_rx)
    return 0;
  return adapter->private_state.retry_pending ? retry_remaining(adapter, now) : UINT32_MAX;
}

static wl_sink_result_t sink(void *context, wl_io_token_t token,
                             const uint8_t *data, size_t length) {
  wl_zephyr_udp_t *adapter = context;
  wl_time_ms_t now;
  (void)token;
  if (active(adapter) != WL_OK) return WL_SINK_FAILED;
  if (length > adapter->private_state.maximum_frame) {
    (void)failed(adapter, WL_ERR_FRAME_TOO_LONG);
    return WL_SINK_FAILED;
  }
  if (adapter->private_state.retry_pending) {
    (void)wl_endpoint_now(adapter->private_state.endpoint, &now);
    if (retry_remaining(adapter, now) != 0U) {
      ++adapter->private_state.stats.tx_deferred;
      return WL_SINK_BUSY;
    }
  }
  TIMING_START();
  const wl_sink_result_t result = wl_udp_socket_send(&adapter->private_state.io, data, length);
  TIMING_FINISH(adapter, send_timing);
  if (result == WL_SINK_BUSY) {
    /* Reuses the pass snapshot inside step; outside step only pressure needs
     * a clock read. There is never a second protocol clock such as uptime. */
    (void)wl_endpoint_now(adapter->private_state.endpoint, &now);
    adapter->private_state.retry_due = now + adapter->private_state.retry_ms;
    adapter->private_state.retry_pending = true;
    ++adapter->private_state.stats.tx_backpressure;
  } else {
    adapter->private_state.retry_pending = false;
    if (result == WL_SINK_SENT) {
      ++adapter->private_state.stats.common.tx_units;
      ++adapter->private_state.stats.common.tx_completions;
      adapter->private_state.stats.common.tx_bytes += length;
    } else {
      (void)failed(adapter, WL_ERR_IO);
    }
  }
  return result;
}

static int receive_service(void *context) {
  wl_zephyr_udp_t *adapter = context;
  wl_err_t result;
  ++adapter->private_state.stats.common.service_calls;
  adapter->private_state.more_rx = false;
  adapter->private_state.rx_paused = false;
  if (adapter->private_state.retry_pending) {
    wl_time_ms_t now;
    (void)wl_endpoint_now(adapter->private_state.endpoint, &now);
    /* Clear even if a cancelled transaction no longer calls the sink. */
    if (retry_remaining(adapter, now) == 0U) adapter->private_state.retry_pending = false;
  }
  wl_ctx_t *link = wl_endpoint_link(adapter->private_state.endpoint);
  for (size_t i = 0; i < adapter->private_state.service_budget; ++i) {
    wl_rx_unit_claim_t claim;
    size_t length;
    result = wl_rx_unit_claim(link, adapter->private_state.maximum_frame + 1U, &claim);
    if (result == WL_ERR_WOULD_BLOCK) {
      adapter->private_state.rx_paused = true;
      ++adapter->private_state.stats.common.rx_backpressure;
      return result;
    }
    if (result != WL_OK) return failed(adapter, result);
    TIMING_START();
    result = wl_udp_socket_receive(&adapter->private_state.io, claim.span.data,
        claim.span.length, adapter->private_state.maximum_frame, &length);
    TIMING_FINISH(adapter, receive_timing);
    if (result == WL_OK) {
      result = wl_rx_unit_commit(link, &claim, length);
      if (result != WL_OK) {
        (void)wl_rx_unit_abort(link, &claim);
        return failed(adapter, result);
      }
      ++adapter->private_state.stats.common.rx_units;
      adapter->private_state.stats.common.rx_bytes += length;
    } else {
      (void)wl_rx_unit_abort(link, &claim);
      if (result == WL_ERR_NO_DATA) {
        if (i == 0) ++adapter->private_state.stats.rx_idle_passes;
        return i == 0 ? WL_ERR_NO_DATA : WL_OK;
      }
      if (result == WL_ERR_BAD_FRAME || result == WL_ERR_FRAME_TOO_LONG)
        ++adapter->private_state.stats.rx_rejected;
      else if (result == WL_ERR_CANCELLED) return result;
      else return failed(adapter, result);
    }
  }
  ++adapter->private_state.stats.service_budget_hits;
  adapter->private_state.more_rx = true;
  return WL_OK;
}

static int service(void *context) {
  wl_zephyr_udp_t *adapter = context;
  const wl_err_t error = active(adapter);
  if (error != WL_OK) return error;
  TIMING_START();
  const int result = receive_service(adapter);
  TIMING_FINISH(adapter, service_timing);
  return result;
}

static void quiesce(void *context) {
  wl_zephyr_udp_t *adapter = context;
  (void)wl_udp_socket_request_stop(&adapter->private_state.io);
  (void)wl_set_sink(wl_endpoint_link(adapter->private_state.endpoint), NULL, NULL);
  /* Do not call the base endpoint's close on behalf of generated RPC owners.
   * Forget this instance now, so a later adapter close cannot affect a new one. */
  adapter->private_state.endpoint = NULL;
  adapter->private_state.retry_pending = false;
  adapter->private_state.more_rx = false;
  adapter->private_state.rx_paused = false;
}

static wl_err_t wait_relative(void *context, uint32_t maximum_ms) {
  wl_zephyr_udp_t *adapter = context;
  wl_err_t result = active(adapter);
  if (result != WL_OK) return result;
  if (adapter->private_state.rx_paused) {
    /* A preceding step may have released slots AFTER service saw a full queue.
     * Probe one empty claim; no receive, parsing or callback is performed. */
    wl_ctx_t *link = wl_endpoint_link(adapter->private_state.endpoint);
    wl_rx_unit_claim_t claim;
    result = wl_rx_unit_claim(link, adapter->private_state.maximum_frame + 1U, &claim);
    if (result == WL_OK) {
      (void)wl_rx_unit_abort(link, &claim);
      adapter->private_state.rx_paused = false;
    } else if (result != WL_ERR_WOULD_BLOCK) return failed(adapter, result);
  }
  ++adapter->private_state.stats.wait_calls;
  result = wl_udp_socket_wait(&adapter->private_state.io,
                              !adapter->private_state.rx_paused, maximum_ms);
  if (result == WL_ERR_NO_DATA) ++adapter->private_state.stats.wait_timeouts;
  else if (result != WL_OK && result != WL_ERR_CANCELLED) return failed(adapter, result);
  return result;
}

static void notify_waiter(void *context) {
  (void)wl_zephyr_udp_notify(context);
}

wl_err_t wl_zephyr_udp_open(wl_zephyr_udp_t *adapter, wl_endpoint_t *endpoint,
                           const wl_zephyr_udp_config_t *config) {
  wl_config_t link_config;
  struct net_sockaddr_in local = {.sin_family = NET_AF_INET};
  struct net_sockaddr_in peer = {.sin_family = NET_AF_INET};
  wl_err_t result;
  if (adapter == NULL || endpoint == NULL || config == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  if (adapter->private_state.io.opened) return WL_ERR_INVALID_STATE;
  wl_ctx_t *link = wl_endpoint_link(endpoint);
  if (link == NULL) return WL_ERR_NOT_INITIALIZED;
  if (wl_endpoint_has_adapter(endpoint) || wl_endpoint_waiter(endpoint) != NULL)
    return WL_ERR_BUSY;
  if (adapter->private_storage.rx_storage == NULL || adapter->private_storage.slot_count < 2 ||
      adapter->private_storage.slot_count > WL_RX_UNIT_QUEUE_MAX_SLOTS ||
      config->peer_address == NULL || config->peer_port == 0 || config->tx_retry_ms > INT32_MAX)
    return WL_ERR_INVALID_ARG;
  (void)wl_get_config(link, &link_config);
  if (link_config.envelope != WL_ENVELOPE_NATIVE_PACKET) return WL_ERR_NOT_SUPPORTED;
  if (link_config.max_payload_len > adapter->private_storage.payload_capacity)
    return WL_ERR_BUF_TOO_SMALL;
  const size_t maximum = wl_frame_raw_size(link_config.max_payload_len, link_config.integrity);
  const size_t ceiling = config->maximum_datagram_size == 0 ? 1472U : config->maximum_datagram_size;
  if (maximum > ceiling || ceiling > 65507U) return WL_ERR_FRAME_TOO_LONG;
  if (zsock_inet_pton(NET_AF_INET, config->peer_address, &peer.sin_addr) != 1 ||
      (config->local_address != NULL &&
       zsock_inet_pton(NET_AF_INET, config->local_address, &local.sin_addr) != 1))
    return WL_ERR_INVALID_ARG;
  local.sin_port = net_htons(config->local_port);
  peer.sin_port = net_htons(config->peer_port);
#if !defined(ZSOCK_IP_DONTFRAG) && defined(CONFIG_NET_IPV4_FRAGMENT)
  /* Zephyr 4.4 has no per-socket IPv4 fragmentation control. */
  return WL_ERR_NOT_SUPPORTED;
#endif
  result = wl_udp_socket_open(&adapter->private_state.io, &local, &peer);
  if (result != WL_OK) return result;
  /* A configured ceiling is not path-MTU discovery. Also forbid IPv4
   * fragmentation in the native stack. On Zephyr 4.4 the stack must be
   * configured without fragmentation; newer stacks can disable it per socket. */
#ifdef ZSOCK_IP_DONTFRAG
  const int dont_fragment = 1;
  if (zsock_setsockopt(adapter->private_state.io.socket_fd, NET_IPPROTO_IP,
      ZSOCK_IP_DONTFRAG, &dont_fragment, sizeof(dont_fragment)) < 0) {
    (void)wl_udp_socket_close(&adapter->private_state.io);
    return WL_ERR_NOT_SUPPORTED;
  }
#endif
  net_socklen_t address_size = sizeof(local);
  if (zsock_getsockname(adapter->private_state.io.socket_fd,
      (struct net_sockaddr *)&local, &address_size) < 0) {
    (void)wl_udp_socket_close(&adapter->private_state.io);
    return WL_ERR_IO;
  }
  const wl_waiter_t waiter = {.wait = wait_relative, .user_data = adapter,
                              .notify = notify_waiter};
  result = wl_endpoint_set_waiter(endpoint, &waiter);
  if (result != WL_OK) {
    (void)wl_udp_socket_close(&adapter->private_state.io);
    return result;
  }
  const wl_rx_unit_queue_config_t queue = {
    .storage = adapter->private_storage.rx_storage,
    .storage_size = (adapter->private_storage.payload_capacity + WL_FRAME_HEADER_SIZE +
        WL_FRAME_MAX_CRC + 1U) * adapter->private_storage.slot_count,
    .unit_size = maximum + 1U, .slot_count = adapter->private_storage.slot_count,
  };
  result = wl_rx_unit_queue_init(link, &queue);
  if (result != WL_OK) {
    (void)wl_endpoint_set_waiter(endpoint, NULL);
    (void)wl_udp_socket_close(&adapter->private_state.io);
    return result;
  }
  memset(&adapter->private_state.stats, 0, sizeof(adapter->private_state.stats));
  atomic_clear(&adapter->private_state.notifications);
  adapter->private_state.endpoint = endpoint;
  adapter->private_state.maximum_frame = maximum;
  adapter->private_state.service_budget = config->service_budget == 0 ? 8U : config->service_budget;
  adapter->private_state.retry_ms = config->tx_retry_ms == 0 ? 1U : config->tx_retry_ms;
  adapter->private_state.local_port = net_ntohs(local.sin_port);
  adapter->private_state.retry_pending = false;
  adapter->private_state.more_rx = false;
  adapter->private_state.rx_paused = false;
  const wl_pump_hooks_t hooks = {.adapter_user_data = adapter, .service = service,
    .quiesce = quiesce, .adapter_deadline_hint = deadline};
  /* All remaining binds are infallible on the checked, fresh owner endpoint:
   * no callbacks or other owner calls can run between preflight and attach. */
  (void)wl_set_sink(link, sink, adapter);
  (void)wl_endpoint_attach(endpoint, &hooks);
  return WL_OK;
}

wl_err_t wl_zephyr_udp_wait(wl_zephyr_udp_t *adapter, uint32_t maximum_ms) {
  wl_poll_hint_t hint;
  wl_err_t result = active(adapter);
  if (result != WL_OK) return result;
  result = wl_endpoint_get_hint(adapter->private_state.endpoint, &hint);
  if (result != WL_OK) return result;
  if (hint.work_pending || hint.next_deadline_ms == 0U) maximum_ms = 0;
  else if (hint.next_deadline_ms < maximum_ms) maximum_ms = hint.next_deadline_ms;
  return wait_relative(adapter, maximum_ms);
}

wl_err_t wl_zephyr_udp_notify(wl_zephyr_udp_t *adapter) {
  if (adapter == NULL) return WL_ERR_INVALID_ARG;
  const wl_err_t result = wl_udp_socket_notify(&adapter->private_state.io);
  if (result == WL_OK) atomic_inc(&adapter->private_state.notifications);
  return result;
}

wl_err_t wl_zephyr_udp_request_stop(wl_zephyr_udp_t *adapter) {
  return adapter == NULL ? WL_ERR_INVALID_ARG : wl_udp_socket_request_stop(&adapter->private_state.io);
}

wl_err_t wl_zephyr_udp_close(wl_zephyr_udp_t *adapter) {
  if (adapter == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  if (adapter->private_state.endpoint != NULL) return WL_ERR_BUSY;
  const wl_err_t result = wl_udp_socket_close(&adapter->private_state.io);
  if (result != WL_OK) return failed(adapter, result);
  adapter->private_state.local_port = 0;
  return WL_OK;
}

uint16_t wl_zephyr_udp_local_port(const wl_zephyr_udp_t *adapter) {
  return adapter == NULL ? 0 : adapter->private_state.local_port;
}

wl_err_t wl_zephyr_udp_get_stats(const wl_zephyr_udp_t *adapter,
                                wl_zephyr_udp_stats_t *stats) {
  if (adapter == NULL || stats == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  *stats = adapter->private_state.stats;
  stats->common.activity_notifications = (uint32_t)atomic_get(&adapter->private_state.notifications);
  stats->common.started = adapter->private_state.io.opened &&
      !atomic_get(&adapter->private_state.io.stopped);
  stats->common.rx_paused = adapter->private_state.rx_paused;
  return WL_OK;
}
