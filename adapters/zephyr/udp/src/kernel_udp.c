/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/kernel_udp.h"
#include "kernel_udp_socket_contract.h"
#include <zephyr/net/socket.h>
#include <limits.h>
#include <string.h>
static uint32_t remaining(const wl_kernel_udp_t *a, uint32_t now) {
  return (int32_t)(now - a->retry_due) >= 0 ? 0U : a->retry_due - now;
}
wl_err_t wl_kernel_udp_open(wl_kernel_udp_t *a, const wl_kernel_udp_config_t *c) {
  if (a == NULL || c == NULL || c->port == NULL || c->clock.now_ms == NULL ||
      c->maximum_datagram_size == 0U || c->maximum_datagram_size > 65507U ||
      c->maximum_datagram_size >= wl_packet_rx_unit_size(c->port) || c->service_budget == 0U ||
      c->service_budget > 16U || c->retry_ms > INT32_MAX)
    return WL_ERR_INVALID_ARG;
  // Initial/reuse storage must be zeroed and previous notifier producers joined.
  if (a->io.opened)
    return WL_ERR_INVALID_STATE;
  const wl_kernel_udp_config_t saved = *c;
  memset(a, 0, sizeof(*a));
  a->config = saved;
  return wl_kernel_udp_socket_open(&a->io, &saved.local, &saved.peer);
}
wl_packet_submit_t wl_kernel_udp_submit(void *user, const uint8_t *data, size_t length,
                                        wl_packet_token_t token) {
  (void)token;
  wl_kernel_udp_t *a = user;
  if (a == NULL || !a->io.opened || atomic_get(&a->io.stopped) ||
      length > a->config.maximum_datagram_size)
    return WL_PACKET_FAILED;
  if (a->retry_pending && remaining(a, a->config.clock.now_ms(a->config.clock.user_data)))
    return WL_PACKET_BUSY;
  wl_packet_submit_t result = wl_kernel_udp_socket_send(&a->io, data, length);
  if (result == WL_PACKET_BUSY) {
    a->retry_due = a->config.clock.now_ms(a->config.clock.user_data) +
                   (a->config.retry_ms ? a->config.retry_ms : 1U);
    a->retry_pending = true;
  } else {
    a->retry_pending = false;
    if (result == WL_PACKET_SENT)
      ++a->sent;
    else
      ++a->errors;
  }
  return result;
}
wl_err_t wl_kernel_udp_service(wl_kernel_udp_t *a, uint32_t now) {
  if (a == NULL || !a->io.opened)
    return WL_ERR_INVALID_STATE;
  if (atomic_get(&a->io.stopped))
    return WL_ERR_CANCELLED;
  a->more_rx = false;
  if (a->retry_pending && remaining(a, now) == 0U) {
    a->retry_pending = false;
    wl_packet_activity(a->config.port);
  }
  for (unsigned i = 0U; i < a->config.service_budget; ++i) {
    if (!wl_kernel_udp_socket_readable(&a->io))
      return WL_OK;
    wl_packet_claim_t claim;
    wl_err_t error =
        wl_packet_rx_claim(a->config.port, a->config.maximum_datagram_size + 1U, &claim);
    if (error == WL_ERR_WOULD_BLOCK)
      return WL_OK;
    if (error != WL_OK)
      return error;
    size_t length = 0U;
    error = wl_kernel_udp_socket_receive(&a->io, claim.span.data, claim.span.length,
                                         a->config.maximum_datagram_size, &length);
    if (error == WL_OK && a->config.decode)
      error = a->config.decode(claim.span.data, length, &length);
    if (error == WL_OK) {
      error = wl_packet_rx_commit(a->config.port, &claim, length);
      if (error != WL_OK) {
        (void)wl_packet_rx_abort(a->config.port, &claim);
        return error;
      }
      ++a->received;
    } else {
      (void)wl_packet_rx_abort(a->config.port, &claim);
      if (error == WL_ERR_NO_DATA)
        return WL_OK;
      if (error == WL_ERR_BAD_FRAME || error == WL_ERR_FRAME_TOO_LONG || error == WL_ERR_CRC ||
          error == WL_ERR_COBS_DECODE)
        ++a->rejected;
      else {
        ++a->errors;
        return error;
      }
    }
  }
  a->more_rx = true;
  return WL_OK;
}
wl_io_readiness_t wl_kernel_udp_readiness(const wl_kernel_udp_t *a, uint32_t now) {
  wl_io_readiness_t r = wl_io_readiness_idle();
  if (a == NULL || !a->io.opened || atomic_get(&a->io.stopped))
    return r;
  if (a->more_rx || wl_kernel_udp_socket_readable(&a->io)) {
    r.runnable = wl_packet_rx_available(a->config.port);
    r.blocked = !r.runnable;
  }
  if (a->retry_pending)
    wl_io_readiness_merge(&r, wl_io_readiness_at(remaining(a, now)));
  return r;
}
wl_err_t wl_kernel_udp_wait(wl_kernel_udp_t *a, uint32_t maximum) {
  if (a == NULL || !a->io.opened)
    return WL_ERR_INVALID_STATE;
  if (atomic_get(&a->io.stopped))
    return WL_ERR_CANCELLED;
  wl_io_readiness_t r = wl_kernel_udp_readiness(a, a->config.clock.now_ms(a->config.clock.user_data));
  if (r.runnable)
    return WL_OK;
  if (r.next_deadline_ms < maximum)
    maximum = r.next_deadline_ms;
  return wl_kernel_udp_socket_wait(&a->io, wl_packet_rx_available(a->config.port), maximum);
}
void wl_kernel_udp_notify(void *user) {
  wl_kernel_udp_t *a = user;
  if (a != NULL && a->io.opened)
    (void)wl_kernel_udp_socket_notify(&a->io);
}
wl_err_t wl_kernel_udp_request_stop(wl_kernel_udp_t *a) {
  return a != NULL ? wl_kernel_udp_socket_request_stop(&a->io) : WL_ERR_INVALID_ARG;
}
wl_err_t wl_kernel_udp_close(wl_kernel_udp_t *a) {
  if (a == NULL)
    return WL_ERR_INVALID_ARG;
  if (!a->io.opened)
    return WL_OK;
  wl_packet_stop(a->config.port);
  return wl_kernel_udp_socket_close(&a->io);
}
uint16_t wl_kernel_udp_local_port(const wl_kernel_udp_t *a) {
  struct net_sockaddr_in address;
  net_socklen_t length = sizeof(address);
  return a != NULL && a->io.opened &&
                 zsock_getsockname(a->io.socket_fd, (struct net_sockaddr *)&address, &length) == 0
             ? net_ntohs(address.sin_port)
             : 0U;
}
